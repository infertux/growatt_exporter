#include <bsd/string.h>
#include <errno.h>
#include <inttypes.h>
#include <limits.h> // INT_MAX
#include <math.h>
#include <modbus-rtu.h>
#include <stdio.h>
#include <stdlib.h>
#include <threads.h>
#include <time.h>

#include "growatt.h"
#include "log.h"

enum {
  MAX_DEVICE_COUNT = 8,
  HOUR = 3600,
  DAY = 24 * HOUR,
};

#define TIMEZONE_OFFSET (7L * HOUR)

#define DEBUG FALSE

#define PROMETHEUS_RESPONSE_SIZE 8192U
#define PROMETHEUS_METRIC_SIZE 256U

enum {
  MODBUS_BAUD = 9600,
  MODBUS_PARITY = 'N',
  MODBUS_DATA_BIT = 8,
  MODBUS_STOP_BIT = 1,
  MODBUS_RESPONSE_TIMEOUT = 3,
};

enum {
  REGISTER_SIZE = 16U,
};

#define add_metric(name, value)                                                                    \
  do {                                                                                             \
    char buffer[PROMETHEUS_METRIC_SIZE];                                                           \
    snprintf(buffer, sizeof(buffer), "# TYPE growatt_%s gauge\ngrowatt_%s{%s} %lf\n", name, name,  \
             device_id_label, value);                                                              \
                                                                                                   \
    const uint16_t len = strlcat(dest, buffer, PROMETHEUS_RESPONSE_SIZE);                          \
    if (len >= PROMETHEUS_RESPONSE_SIZE) {                                                         \
      return query_device_failed(ctx, device_id, "buffer overflow");                               \
    }                                                                                              \
                                                                                                   \
    if (strcmp(name, "read_metric_failed_total") != 0 &&                                           \
        strcmp(name, "read_metric_succeeded_total") != 0) {                                        \
      read_metric_succeeded_total[device_id]++;                                                    \
    }                                                                                              \
  } while (0)

// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
char device_metrics[MAX_DEVICE_COUNT][PROMETHEUS_RESPONSE_SIZE * sizeof(char)] = {{'\0'}};

// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
uint8_t read_metric_failed_total[MAX_DEVICE_COUNT] = {0};
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
uint8_t read_metric_succeeded_total[MAX_DEVICE_COUNT] = {0};

// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
time_t last_time_synced_at[MAX_DEVICE_COUNT] = {0};
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
time_t last_time_read_settings_at[MAX_DEVICE_COUNT] = {0};

#define modbus_read_holding_registers modbus_read_registers
#define modbus_write_holding_registers modbus_write_registers

int read_holding_register_scaled_by(modbus_t *ctx, const int addr, double *value, double scale) {
  uint16_t buffer[1] = {0};
  int ret = modbus_read_holding_registers(ctx, addr, 1, buffer);
  *value = (double)buffer[0] * scale;

  return ret;
}

int read_holding_register_double_scaled_by(modbus_t *ctx, const int addr, double *value,
                                           double scale) {
  uint16_t buffer[2] = {0, 0};
  int ret = modbus_read_holding_registers(ctx, addr, 2, buffer);
  // NOLINTNEXTLINE(hicpp-signed-bitwise)
  *value = ((double)(buffer[0] << REGISTER_SIZE) + (double)(buffer[1])) * scale;

  return ret;
}

int read_input_register_scaled_by(modbus_t *ctx, const int addr, double *value, double scale) {
  uint16_t buffer[1] = {0};
  int ret = modbus_read_input_registers(ctx, addr, 1, buffer);
  *value = (double)buffer[0] * scale;

  return ret;
}

int read_input_register_double_scaled_by(modbus_t *ctx, const int addr, double *value,
                                         double scale) {
  uint16_t buffer[2] = {0, 0};
  int ret = modbus_read_input_registers(ctx, addr, 2, buffer);
  // NOLINTNEXTLINE(hicpp-signed-bitwise)
  *value = ((double)(buffer[0] << REGISTER_SIZE) + (double)(buffer[1])) * scale;

  return ret;
}

inline void print_register(FILE *stream, const uint16_t *reg, const uint8_t size) {
  for (int i = 0; i < size - 1; i++) {
    fprintf(stream, "%04X·", reg[i]);
  }
  fprintf(stream, "%04X", reg[size - 1]);
}

void clock_write(modbus_t *ctx) {
  const time_t now = time(NULL) + TIMEZONE_OFFSET + 2; // adding 2 seconds because writing registers
                                                       // is slow so we need to compensate for it
  struct tm now_tm;
  gmtime_r(&now, &now_tm);

  const uint16_t clock[REGISTER_CLOCK_SIZE] = {
      now_tm.tm_year - REGISTER_CLOCK_YEAR_OFFSET,
      now_tm.tm_mon + 1,
      now_tm.tm_mday,
      now_tm.tm_hour,
      now_tm.tm_min,
      now_tm.tm_sec,
  };

  fprintf(LOG_DEBUG, "About to write ");
  print_register(LOG_DEBUG, clock, REGISTER_CLOCK_SIZE);
  fprintf(LOG_DEBUG, " into clock register\n");

  if (REGISTER_CLOCK_SIZE !=
      modbus_write_holding_registers(ctx, REGISTER_CLOCK_ADDRESS, REGISTER_CLOCK_SIZE, clock)) {
    fprintf(LOG_ERROR, "Writing clock failed\n");
  } else {
    fprintf(LOG_INFO, "Writing clock succeeded\n");
  }
}

int clock_sync(modbus_t *ctx) {
  uint16_t clock[REGISTER_CLOCK_SIZE] = {0};
  if (-1 ==
      modbus_read_holding_registers(ctx, REGISTER_CLOCK_ADDRESS, REGISTER_CLOCK_SIZE, clock)) {
    fprintf(LOG_ERROR, "Reading clock failed\n");
    return INT_MAX;
  }

  fprintf(LOG_DEBUG, "Clock register is ");
  print_register(LOG_DEBUG, clock, REGISTER_CLOCK_SIZE);
  fprintf(LOG_DEBUG, "\n");

  struct tm clock_tm = {
      clock[REGISTER_CLOCK_SIZE - 1],        // seconds
      clock[4],                              // minutes
      clock[3],                              // hours
      clock[2],                              // day
      clock[1] - 1,                          // month
      clock[0] + REGISTER_CLOCK_YEAR_OFFSET, // year
  };
  const time_t clock_time_t = mktime(&clock_tm);
  const time_t now = time(NULL) + TIMEZONE_OFFSET;
  const double difference = difftime(clock_time_t, now);

  if (fabs(difference) >= CLOCK_OFFSET_THRESHOLD) {
    fprintf(LOG_ERROR, "Device time is %.0lfs ahead!\n", difference);

    char time_string[sizeof "2011-10-08T07:07:09Z"];
    strftime(time_string, sizeof time_string, "%FT%T", &clock_tm);
    fprintf(LOG_ERROR, "device time = %s\n", time_string);

    struct tm now_tm;
    gmtime_r(&now, &now_tm);
    strftime(time_string, sizeof time_string, "%FT%T", &now_tm);
    fprintf(LOG_ERROR, "        now = %s\n", time_string);

    clock_write(ctx);

    return (int)difference;
  }

  fprintf(LOG_INFO, "Device time is %.0lfs ahead\n", difference);

  return EXIT_SUCCESS;
}

int query_device_failed(modbus_t *ctx, const uint8_t device_id, const char *message) {
  if (errno) {
    fprintf(LOG_ERROR, "[Device %" PRIu8 "] %s: %s (%d)\n", device_id, message,
            modbus_strerror(errno), errno);
  }

  if (ctx) {
    modbus_free(ctx);
  }

  return (errno ? errno : 9001); // NOLINT: 9001 is an unassigned modbus errno
}

void read_register_failed(const uint8_t device_id, const REGISTER *reg) {
  read_metric_failed_total[device_id]++;

  fprintf(LOG_ERROR, "[Device %" PRIu8 "] Reading register %" PRIu8 " (%s) failed", device_id,
          reg->address, reg->human_name);

  if (errno) {
    fprintf(LOG_ERROR, ": %s (%d)", modbus_strerror(errno), errno);
  }

  fprintf(LOG_ERROR, "\n");
}

int query_device_thread(void *id_ptr) {
  const uint8_t device_id = *((uint8_t *)id_ptr);
  fprintf(LOG_DEBUG, "Querying device ID %" PRIu8 " in thread ID %lu...\n", device_id,
          thrd_current());

  read_metric_failed_total[device_id] = 0;
  read_metric_succeeded_total[device_id] = 0;

  char *dest = device_metrics[device_id];
  *dest = '\0'; // empty buffer content from previous queries

  char
      device_id_label[32]; // NOLINT: 32 chars should never overflow since MAX_DEVICE_COUNT is small
  snprintf(device_id_label, sizeof(device_id_label), "device_id=\"%" PRIu8 "\"", device_id);

  modbus_t *ctx = NULL;
  ctx =
      modbus_new_rtu("/dev/ttyUSB0", MODBUS_BAUD, MODBUS_PARITY, MODBUS_DATA_BIT, MODBUS_STOP_BIT);
  if (ctx == NULL) {
    return query_device_failed(ctx, device_id, "Unable to create the libmodbus context");
  }

  if (modbus_set_debug(ctx, DEBUG)) {
    return query_device_failed(ctx, device_id, "Set debug flag failed");
  }

  if (modbus_set_slave(ctx, 1)) { // required with RTU mode
    return query_device_failed(ctx, device_id, "Set slave failed");
  }

  modbus_set_response_timeout(ctx, MODBUS_RESPONSE_TIMEOUT, 0); // in seconds

  if (modbus_connect(ctx)) {
    return query_device_failed(ctx, device_id, "Connection failed");
  }

  const time_t now = time(NULL);

  fprintf(LOG_DEBUG, "last_time_synced_at[%" PRIu8 "] = %lf\n", device_id,
          difftime(now, last_time_synced_at[device_id]));
  if (difftime(now, last_time_synced_at[device_id]) > 1 * DAY) {
    if (clock_sync(ctx)) {
      fprintf(LOG_ERROR, "Synced time\n");
    }

    last_time_synced_at[device_id] = now;
  }

  fprintf(LOG_DEBUG, "last_time_read_settings_at[%" PRIu8 "] = %lf\n", device_id,
          difftime(now, last_time_read_settings_at[device_id]));
  if (difftime(now, last_time_read_settings_at[device_id]) > 1 * HOUR) {
    const uint8_t register_count = sizeof(holding_registers) / sizeof(REGISTER);
    for (uint8_t index = 0; index < register_count; index++) {
      const REGISTER reg = holding_registers[index];
      int ret = -1;
      double result[] = {0, 0};

      switch (reg.register_size) {
      case REGISTER_SINGLE:
        ret = read_holding_register_scaled_by(ctx, reg.address, result, reg.scale);
        break;

      case REGISTER_DOUBLE:
        ret = read_holding_register_double_scaled_by(ctx, reg.address, result, reg.scale);
        break;

      default:
        return query_device_failed(ctx, device_id, "Unexpected register size");
      }

      if (-1 == ret) {
        read_register_failed(device_id, &reg);
      } else {
        add_metric(reg.metric_name, *result);
      }
    }

    last_time_read_settings_at[device_id] = now;
  }

  const uint8_t register_count = sizeof(input_registers) / sizeof(REGISTER);
  for (uint8_t index = 0; index < register_count; index++) {
    const REGISTER reg = input_registers[index];
    int ret = -1;
    double result[] = {0, 0};

    switch (reg.register_size) {
    case REGISTER_SINGLE:
      ret = read_input_register_scaled_by(ctx, reg.address, result, reg.scale);
      break;

    case REGISTER_DOUBLE:
      ret = read_input_register_double_scaled_by(ctx, reg.address, result, reg.scale);
      break;

    default:
      return query_device_failed(ctx, device_id, "Unexpected register size");
    }

    if (-1 == ret) {
      read_register_failed(device_id, &reg);
    } else {
      add_metric(reg.metric_name, *result);
    }
  }

  add_metric("read_metric_failed_total", (double)read_metric_failed_total[device_id]);
  add_metric("read_metric_succeeded_total", (double)read_metric_succeeded_total[device_id]);

  modbus_close(ctx);
  modbus_free(ctx);

  return EXIT_SUCCESS;
}

int query(char *dest, const uint8_t *device_ids) {
  *dest = '\0'; // empty buffer content from previous queries

  int count = 0;
  while (device_ids[++count]) // NOLINT: counting IDs until we get a zero value
    ;
  fprintf(LOG_DEBUG, "Found %d device IDs to query\n", count);

  thrd_t threads[MAX_DEVICE_COUNT];

  for (int i = 0; i < count; i++) {
    int status =
        thrd_create(&threads[i], (thrd_start_t)query_device_thread, (void *)(&(device_ids[i])));
    if (status != thrd_success) {
      fprintf(LOG_ERROR, "thrd_create() failed\n");
      return EXIT_FAILURE;
    }
  }

  for (int i = 0; i < count; i++) {
    int result = 0;
    const int thrd_ret = thrd_join(threads[i], &result);

    if (thrd_ret != thrd_success || result != EXIT_SUCCESS) {
      fprintf(LOG_ERROR, "Thread %d failed (code = %d)\n", i, result);
      return EXIT_FAILURE;
    }

    fprintf(LOG_DEBUG, "Thread %d succeeded (code = %d)\n", i, result);
    const uint8_t device_id = device_ids[i];
    const char *metrics = device_metrics[device_id];
    fprintf(LOG_DEBUG, "Got metrics from device ID %" PRIu8 " (%zu bytes)\n", device_id,
            strlen(metrics));

    const uint16_t len = strlcat(dest, metrics, PROMETHEUS_RESPONSE_SIZE);
    if (len >= PROMETHEUS_RESPONSE_SIZE) {
      fprintf(LOG_ERROR, "buffer overflow: %" PRIu16 " >= %i\n", len, PROMETHEUS_RESPONSE_SIZE);

      return ENAMETOOLONG;
    }
  }

  return EXIT_SUCCESS;
}
