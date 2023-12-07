#ifndef GROWATT_MODBUS_H
#define GROWATT_MODBUS_H

#include <assert.h>
#include <bsd/string.h>
#include <errno.h>
#include <inttypes.h>
#include <limits.h> // INT_MAX
#include <math.h>
#include <modbus.h>
#include <stdio.h>
#include <stdlib.h>
#include <threads.h>
#include <time.h>
#include <unistd.h> // sleep()

#include "growatt.h"
#include "log.h"

enum {
  REFRESH_PERIOD = 10, // seconds
  HOUR = 3600,
  DAY = 24 * HOUR,
};

#define TIMEZONE_OFFSET (7L * HOUR) // TODO: pass as argument

#define DEBUG FALSE

#define METRIC_BUFFER_SIZE 256U
#define RESPONSE_BUFFER_SIZE 8192U

enum {
  MODBUS_BAUD = 9600,
  MODBUS_PARITY = 'N',
  MODBUS_DATA_BIT = 8,
  MODBUS_STOP_BIT = 1,
  MODBUS_RESPONSE_TIMEOUT = 200000U, // in us
};

enum {
  REGISTER_SIZE = 16U,
  HEX_SIZE = 8U, // bytes for hex representation
};

typedef struct __attribute__((aligned(METRIC_BUFFER_SIZE / 2))) {
  char name[METRIC_BUFFER_SIZE];
  double value;
} METRIC;

/**
 * Use (blocking) mutex to access struct
 */
typedef struct __attribute__((aligned(METRIC_BUFFER_SIZE / 2))) {
  mtx_t mutex;
  METRIC *metrics;
  /** Number of metrics stored in array (for internal use) */
  size_t size;
  /** Number of metrics successfully read */
  size_t read_metric_succeeded_total;
  /** Number of metrics failed to read */
  size_t read_metric_failed_total;
  /** Timestamp of last clock synchronization check */
  time_t last_time_synced_at;
  /** Timestamp of last time settings were queried */
  time_t last_time_read_settings_at;
} METRICS;

// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
METRICS device_metrics;

// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
static modbus_t *ctx = NULL;

void add_metric(char const name[static 1], const double value) {
  METRIC metric;
  strlcpy(metric.name, name, METRIC_BUFFER_SIZE);
  metric.value = value;

  METRIC *new_metrics = realloc(device_metrics.metrics, (device_metrics.size + 1) * sizeof(metric));
  if (new_metrics == NULL) {
    PERROR("realloc failed");
    exit(errno);
  }
  device_metrics.metrics = new_metrics;
  device_metrics.metrics[device_metrics.size++] = metric;

  if (strcmp(name, "read_metric_failed_total") != 0 && strcmp(name, "read_metric_succeeded_total") != 0) {
    device_metrics.read_metric_succeeded_total++;
  }
}

#define modbus_read_holding_registers modbus_read_registers
#define modbus_write_holding_registers modbus_write_registers

ssize_t read_holding_register_scaled_by(modbus_t *ctx, const int addr, double *value, double scale) {
  uint16_t buffer[1] = {0};
  ssize_t ret = modbus_read_holding_registers(ctx, addr, 1, buffer);
  *value = (double)buffer[0] * scale;

  return ret;
}

ssize_t read_holding_register_double_scaled_by(modbus_t *ctx, const int addr, double *value, double scale) {
  uint16_t buffer[2] = {0};
  ssize_t ret = modbus_read_holding_registers(ctx, addr, 2, buffer);
  // NOLINTNEXTLINE(hicpp-signed-bitwise)
  *value = ((double)(buffer[0] << REGISTER_SIZE) + (double)(buffer[1])) * scale;

  return ret;
}

ssize_t read_input_register_scaled_by(modbus_t *ctx, const int addr, double *value, double scale) {
  uint16_t buffer[1] = {0};
  ssize_t ret = modbus_read_input_registers(ctx, addr, 1, buffer);
  *value = (double)buffer[0] * scale;

  return ret;
}

ssize_t read_input_register_double_scaled_by(modbus_t *ctx, const int addr, double *value, double scale) {
  uint16_t buffer[2] = {0};
  ssize_t ret = modbus_read_input_registers(ctx, addr, 2, buffer);
  // NOLINTNEXTLINE(hicpp-signed-bitwise)
  *value = ((double)(buffer[0] << REGISTER_SIZE) + (double)(buffer[1])) * scale;

  return ret;
}

void print_register(char *dest, const uint16_t *reg, const uint8_t size) {
  assert(size < HEX_SIZE);
  char buffer[HEX_SIZE];
  for (int i = 0; i < size; i++) {
    sprintf(buffer, "%04X·", reg[i]);
    strlcat(dest, buffer, (size_t)REGISTER_CLOCK_SIZE * HEX_SIZE);
  }
  dest[strlen(dest) - 1] = 0; // delete last '·'
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

  char hex[REGISTER_CLOCK_SIZE * HEX_SIZE] = {0};
  print_register(hex, clock, REGISTER_CLOCK_SIZE);
  LOG(LOG_DEBUG, "About to write %s into clock register", hex);

  if (REGISTER_CLOCK_SIZE != modbus_write_holding_registers(ctx, REGISTER_CLOCK_ADDRESS, REGISTER_CLOCK_SIZE, clock)) {
    PERROR("Writing clock failed");
  } else {
    LOG(LOG_INFO, "Writing clock succeeded");
  }
}

int clock_sync(modbus_t *ctx) {
  uint16_t clock[REGISTER_CLOCK_SIZE] = {0};
  if (-1 == modbus_read_holding_registers(ctx, REGISTER_CLOCK_ADDRESS, REGISTER_CLOCK_SIZE, clock)) {
    PERROR("Reading clock failed");
    return INT_MAX;
  }

  char hex[REGISTER_CLOCK_SIZE * HEX_SIZE] = {0};
  print_register(hex, clock, REGISTER_CLOCK_SIZE);
  LOG(LOG_DEBUG, "Clock register is %s", hex);

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
    LOG(LOG_ERROR, "Device time is %.0lfs ahead!", difference);

    char time_string[sizeof "2011-10-08T07:07:09Z"];
    strftime(time_string, sizeof time_string, "%FT%T", &clock_tm);
    LOG(LOG_ERROR, "device time = %s", time_string);

    struct tm now_tm;
    gmtime_r(&now, &now_tm);
    strftime(time_string, sizeof time_string, "%FT%T", &now_tm);
    LOG(LOG_ERROR, "        now = %s", time_string);

    clock_write(ctx);

    return (int)difference;
  }

  LOG(LOG_INFO, "Device time is %.0lfs ahead", difference);

  return EXIT_SUCCESS;
}

int query_device_failed(modbus_t *ctx, char const message[static 1]) {
  if (errno) {
    PERROR("%s: %s (%d)", message, modbus_strerror(errno), errno);
  }

  if (ctx) {
    modbus_free(ctx);
  }

  return (errno ? errno : 9001); // NOLINT: 9001 is an unassigned modbus errno
}

void read_register_failed(const REGISTER *reg) {
  device_metrics.read_metric_failed_total++;

  LOG(LOG_ERROR, "Reading register %" PRIu8 " (%s) failed", reg->address, reg->human_name);

  if (errno) {
    PERROR("%s (%d)", modbus_strerror(errno), errno);
  }
}

int query_modbus(modbus_t *ctx) {
  if (device_metrics.metrics) {
    free(device_metrics.metrics);
  }

  device_metrics.metrics = malloc(sizeof(METRIC));
  device_metrics.size = 0;
  device_metrics.read_metric_failed_total = 0;
  device_metrics.read_metric_succeeded_total = 0;

  const time_t now = time(NULL);

  LOG(LOG_TRACE, "now - last_time_synced_at = %.0lfs", difftime(now, device_metrics.last_time_synced_at));
  if (difftime(now, device_metrics.last_time_synced_at) > 1 * DAY) {
    if (clock_sync(ctx)) {
      LOG(LOG_INFO, "Synced time");
    }

    device_metrics.last_time_synced_at = now;
  }

  LOG(LOG_TRACE, "now - last_time_read_settings_at = %.0lfs", difftime(now, device_metrics.last_time_read_settings_at));
  if (difftime(now, device_metrics.last_time_read_settings_at) > 1 * HOUR) {
    for (size_t index = 0; index < COUNT(holding_registers); index++) {
      const REGISTER reg = holding_registers[index];
      ssize_t ret = -1;
      double result[2] = {0};

      switch (reg.register_size) {
      case REGISTER_SINGLE:
        ret = read_holding_register_scaled_by(ctx, reg.address, result, reg.scale);
        break;

      case REGISTER_DOUBLE:
        ret = read_holding_register_double_scaled_by(ctx, reg.address, result, reg.scale);
        break;

      default:
        return query_device_failed(ctx, "Unexpected register size");
      }

      if (-1 == ret) {
        read_register_failed(&reg);
      } else {
        add_metric(reg.metric_name, *result);
      }
    }

    device_metrics.last_time_read_settings_at = now;
  }

  for (size_t index = 0; index < COUNT(input_registers); index++) {
    const REGISTER reg = input_registers[index];
    ssize_t ret = -1;
    double result[2] = {0};

    switch (reg.register_size) {
    case REGISTER_SINGLE:
      ret = read_input_register_scaled_by(ctx, reg.address, result, reg.scale);
      break;

    case REGISTER_DOUBLE:
      ret = read_input_register_double_scaled_by(ctx, reg.address, result, reg.scale);
      break;

    default:
      return query_device_failed(ctx, "Unexpected register size");
    }

    if (-1 == ret) {
      read_register_failed(&reg);
    } else {
      add_metric(reg.metric_name, *result);
    }
  }

  add_metric("read_metric_failed_total", (double)device_metrics.read_metric_failed_total);
  add_metric("read_metric_succeeded_total", (double)device_metrics.read_metric_succeeded_total);

  return device_metrics.read_metric_succeeded_total == 0 ? EXIT_FAILURE : EXIT_SUCCESS;
}

static void stop_modbus_thread(void) {
  modbus_close(ctx);
  modbus_free(ctx);
}

int start_modbus_thread(char device_or_uri[static 1]) {
  LOG(LOG_DEBUG, "Modbus thread running...");

  if (atexit(stop_modbus_thread)) {
    PERROR("Could not register cleanup routine");
    return EXIT_FAILURE;
  }

  if (mtx_init(&device_metrics.mutex, mtx_plain) != thrd_success) {
    PERROR("Could not initialize modbus mutex");
    return EXIT_FAILURE;
  }

  char modbus_tcp_host[256]; // NOLINT(readability-magic-numbers)
  int modbus_tcp_port = 0;
  if (device_or_uri[0] != '/') {
    // when starting with '/', assume it's a device like "/dev/ttyUSB"
    // otherwise try to parse it as a "hostname:port" like "example.com:1234"
    sscanf(device_or_uri, "%255[^:]:%d", modbus_tcp_host, &modbus_tcp_port); // NOLINT(cert-err34-c)

    if (modbus_tcp_port < 1 || modbus_tcp_port > USHRT_MAX) {
      return query_device_failed(ctx, "Invalid port number");
    }
  }

  if (modbus_tcp_port) {
    ctx = modbus_new_tcp(modbus_tcp_host, modbus_tcp_port);
  } else {
    ctx = modbus_new_rtu(device_or_uri, MODBUS_BAUD, MODBUS_PARITY, MODBUS_DATA_BIT, MODBUS_STOP_BIT);
    if (modbus_set_slave(ctx, 1)) { // required with RTU mode
      return query_device_failed(ctx, "Set slave failed");
    }
  }

  if (ctx == NULL) {
    return query_device_failed(ctx, "Unable to create the libmodbus context");
  }

  if (modbus_set_debug(ctx, DEBUG)) {
    return query_device_failed(ctx, "Set debug flag failed");
  }

  if (modbus_set_response_timeout(ctx, 0, MODBUS_RESPONSE_TIMEOUT)) {
    return query_device_failed(ctx, "Set response timeout failed");
  }

  if (modbus_connect(ctx)) {
    return query_device_failed(ctx, "Modbus connection failed");
  }

  struct timespec before, after; // NOLINT(readability-isolate-declaration)

  while (keep_running) {
    clock_gettime(CLOCK_REALTIME, &before);
    LOG(LOG_INFO, "Querying device %s...", device_or_uri);

    mtx_lock(&device_metrics.mutex);
    int result = query_modbus(ctx);
    mtx_unlock(&device_metrics.mutex);

    if (result != EXIT_SUCCESS) {
      PERROR("query_modbus() failed (code = %d)", result);
      return EXIT_FAILURE;
    }

    clock_gettime(CLOCK_REALTIME, &after);
    double const elapsed = after.tv_sec - before.tv_sec + (double)(after.tv_nsec - before.tv_nsec) / 1e9; // NOLINT

    LOG(LOG_INFO, "Got %lu/%lu metrics in %.1fs", device_metrics.read_metric_succeeded_total,
        device_metrics.read_metric_succeeded_total + device_metrics.read_metric_failed_total, elapsed);

    /*
    LOG(LOG_TRACE, "last_time_synced_at = %ld, last_time_read_settings_at = %ld", device_metrics.last_time_synced_at,
        device_metrics.last_time_read_settings_at);

    for (size_t i = 0; i < device_metrics.size; i++) {
      METRIC metric = device_metrics.metrics[i];
      LOG(LOG_TRACE, "%s = %lf", metric.name, metric.value);
    }
    */

    LOG(LOG_INFO, "Waiting %d seconds...", REFRESH_PERIOD);
    for (size_t i = 0; i < REFRESH_PERIOD; i++) {
      sleep(1); // NOLINT(concurrency-mt-unsafe)
      if (!keep_running) {
        return EXIT_SUCCESS;
      }
    }
  }

  return EXIT_SUCCESS;
}

#endif /* GROWATT_MODBUS_H */
