#include <bsd/string.h>
#include <errno.h>
#include <inttypes.h>
#include <limits.h> // INT_MAX
#include <modbus-rtu.h>
#include <stdio.h>
#include <stdlib.h>
#include <threads.h>
#include <time.h>

#include "epever.h"

#define TIMEZONE_BIAS (7 * HOURS)
#define MAX_DEVICE_ID 100

#define HOUR 3600
#define HOURS HOUR

#define DEBUG FALSE

#define PROMETHEUS_RESPONSE_SIZE 4096
#define PROMETHEUS_METRIC_SIZE 256

#define MODBUS_RESPONSE_TIMEOUT 5

#define REGISTER_SIZE 16U
#define REGISTER_HALF_SIZE (REGISTER_SIZE / 2)
#define REGISTER_HALF_MASK 0xFF

#define REGISTER_RATED_INPUT_CURRENT 0x3001

#define REGISTER_PV_VOLTAGE 0x3100
#define REGISTER_PV_CURRENT 0x3101
#define REGISTER_PV_POWER 0x3102

#define REGISTER_BATTERY_VOLTAGE 0x3104
#define REGISTER_BATTERY_CURRENT 0x3105
#define REGISTER_BATTERY_POWER 0x3106
#define REGISTER_BATTERY_TEMPERATURE 0x3110
#define REGISTER_DEVICE_TEMPERATURE 0x3111
#define REGISTER_BATTERY_SOC 0x311A

#define REGISTER_BATTERY_STATUS 0x3200
#define REGISTER_CHARGING_STATUS 0x3201

#define REGISTER_ENERGY_GENERATED_TODAY 0x330C
#define REGISTER_ENERGY_GENERATED_TOTAL 0x3312

#define REGISTER_SETTINGS_BOOST_VOLTAGE 0x9007
#define REGISTER_SETTINGS_FLOAT_VOLTAGE 0x9008
#define REGISTER_SETTINGS_BOOST_RECONNECT_VOLTAGE 0x9009

#define REGISTER_SETTINGS_LENGTH_OF_NIGHT 0x9065

#define REGISTER_CLOCK 0x9013

#define add_metric(name, value)                                                \
  do {                                                                         \
    char buffer[PROMETHEUS_METRIC_SIZE];                                       \
    snprintf(buffer, sizeof(buffer), "# TYPE %s gauge\n%s%s %lf\n", name,      \
             name, labels, value);                                             \
    strcat(dest, buffer);                                                      \
  } while (0)

char device_metrics[MAX_DEVICE_ID + 1]
                   [PROMETHEUS_RESPONSE_SIZE * sizeof(char)] = {{'\0'}};

time_t last_time_synced_at[MAX_DEVICE_ID + 1] = {0};
time_t last_time_read_settings_at[MAX_DEVICE_ID + 1] = {0};

#define read_input_registers modbus_read_input_registers
#define read_holding_registers modbus_read_registers

int read_input_register(modbus_t *ctx, const int addr, double *value) {
  uint16_t buffer[1] = {0};
  int ret = read_input_registers(ctx, addr, 1, buffer);
  *value = (double)buffer[0];

  return ret;
}

int read_holding_register(modbus_t *ctx, const int addr, double *value) {
  uint16_t buffer[1] = {0};
  int ret = read_holding_registers(ctx, addr, 1, buffer);
  *value = (double)buffer[0];

  return ret;
}

int read_input_register_scaled_by(modbus_t *ctx, const int addr, double *value,
                                  double scale) {
  int ret = read_input_register(ctx, addr, value);
  *value /= scale;

  return ret;
}

int read_holding_register_scaled_by(modbus_t *ctx, const int addr,
                                    double *value, double scale) {
  int ret = read_holding_register(ctx, addr, value);
  *value /= scale;

  return ret;
}

int read_input_register_scaled(modbus_t *ctx, const int addr, double *value) {
  return read_input_register_scaled_by(ctx, addr, value, 100.0);
}

int read_holding_register_scaled(modbus_t *ctx, const int addr, double *value) {
  return read_holding_register_scaled_by(ctx, addr, value, 100.0);
}

int read_input_register_double_scaled_by(modbus_t *ctx, const int addr,
                                         double *value, double scale) {
  uint16_t buffer[2] = {0, 0};
  int ret = read_input_registers(ctx, addr, 2, buffer);
  *value = ((double)(buffer[1] << REGISTER_SIZE) + (double)(buffer[0])) / scale;

  return ret;
}

int read_input_register_double_scaled(modbus_t *ctx, const int addr,
                                      double *value) {
  return read_input_register_double_scaled_by(ctx, addr, value, 100.0);
}

int sync_time(modbus_t *ctx) {
  uint16_t clock[3] = {0, 0, 0};
  if (-1 == read_holding_registers(ctx, REGISTER_CLOCK, 3, clock)) {
    fprintf(FD_ERROR, "Reading clock failed\n");
    return INT_MAX;
  }

  struct tm clock_tm = {
      clock[0] & REGISTER_HALF_MASK,          // seconds
      clock[0] >> REGISTER_HALF_SIZE,         // minutes
      clock[1] & REGISTER_HALF_MASK,          // hours
      clock[1] >> REGISTER_HALF_SIZE,         // day
      (clock[2] & REGISTER_HALF_MASK) - 1,    // month
      (clock[2] >> REGISTER_HALF_SIZE) + 100, // year
  };
  const time_t clock_time_t = mktime(&clock_tm);
  const time_t now = time(NULL) + TIMEZONE_BIAS;
  const double difference = difftime(clock_time_t, now);

  if (difference > 30) {
    fprintf(FD_ERROR, "Device time is %.0lfs ahead!\n", difference);

    char time_string[sizeof "2011-10-08T07:07:09Z"];
    strftime(time_string, sizeof time_string, "%FT%T", &clock_tm);
    fprintf(FD_ERROR, "device time = %s\n", time_string);

    const struct tm *now_tm = gmtime(&now);
    strftime(time_string, sizeof time_string, "%FT%T", now_tm);
    fprintf(FD_ERROR, "        now = %s\n", time_string);

    // TODO: sync device time

    return (int)difference;
  }

  return EXIT_SUCCESS;
}

int query_device_failed(modbus_t *ctx, char *error) {
  if (errno) {
    fprintf(FD_ERROR, "%s: %s (%d)\n", error, modbus_strerror(errno), errno);
  }

  if (ctx) {
    modbus_free(ctx);
  }

  return (errno ? errno : 9001);
}

int query_device_thread(void *id_ptr) {
  const uint8_t id = *((uint8_t *)id_ptr);
  fprintf(FD_DEBUG, "Querying device ID %" PRIu8 " in thread ID %lu...\n", id,
          thrd_current());

  char *dest = device_metrics[id];
  *dest = '\0'; // empty buffer content from previous queries

  char labels[32];
  snprintf(labels, sizeof(labels), "{device_id=\"%" PRIu8 "\"}", id);

  modbus_t *ctx = NULL;
  // XXX: ideally we could use TCP but this always times out for some reason:
  // ctx = modbus_new_tcp("192.168.1.X", 8088);
  // ... so we use socat:
  // socat -ls -v pty,link=/tmp/ttyepever123 tcp:192.168.1.X:8088
  char path[32];
  snprintf(path, sizeof(path), "/tmp/ttyepever%" PRIu8, id);
  ctx = modbus_new_rtu(path, 115200, 'N', 8, 1);
  if (ctx == NULL) {
    return query_device_failed(ctx, "Unable to create the libmodbus context");
  }

  if (modbus_set_debug(ctx, DEBUG)) {
    return query_device_failed(ctx, "Set debug flag failed");
  }

  if (modbus_set_slave(ctx, 1)) { // required with RTU mode
    return query_device_failed(ctx, "Set slave failed");
  }

  modbus_set_response_timeout(ctx, MODBUS_RESPONSE_TIMEOUT, 0); // in seconds

  if (modbus_connect(ctx)) {
    return query_device_failed(ctx, "Connection failed");
  }

  const time_t now = time(NULL);

  fprintf(FD_DEBUG, "last_time_synced_at[%" PRIu8 "] = %lf\n", id,
          difftime(now, last_time_synced_at[id]));
  if (difftime(now, last_time_synced_at[id]) > 24 * HOURS) {
    // if (sync_time(ctx)) {
    //  fprintf(FD_ERROR, "Synced time");
    //}

    last_time_synced_at[id] = now;
  }

  double battery_status = 0;
  if (-1 ==
      read_input_register(ctx, REGISTER_BATTERY_STATUS, &battery_status)) {
    fprintf(FD_ERROR, "Reading battery status failed\n");
  } else {
    add_metric("epever_battery_status", battery_status);
  }

  double charging_status = 0;
  if (-1 ==
      read_input_register(ctx, REGISTER_CHARGING_STATUS, &charging_status)) {
    fprintf(FD_ERROR, "Reading charging status failed\n");
  } else {
    add_metric("epever_charging_status", charging_status);
  }

  double pv_voltage = 0;
  if (-1 == read_input_register_scaled(ctx, REGISTER_PV_VOLTAGE, &pv_voltage)) {
    fprintf(FD_ERROR, "Reading PV voltage failed\n");
  } else {
    add_metric("epever_pv_volts", pv_voltage);
  }

  double pv_current = 0;
  if (-1 == read_input_register_scaled(ctx, REGISTER_PV_CURRENT, &pv_current)) {
    fprintf(FD_ERROR, "Reading PV current failed\n");
  } else {
    add_metric("epever_pv_amperes", pv_current);
  }

  double pv_power = 0;
  if (-1 ==
      read_input_register_double_scaled(ctx, REGISTER_PV_POWER, &pv_power)) {
    fprintf(FD_ERROR, "Reading PV power failed\n");
  } else {
    add_metric("epever_pv_watts", pv_power);
  }

  double energy_generated_today = 0;
  if (-1 ==
      read_input_register_double_scaled_by(ctx, REGISTER_ENERGY_GENERATED_TODAY,
                                           &energy_generated_today, 0.1)) {
    fprintf(FD_ERROR, "Reading energy generated today failed\n");
  } else {
    add_metric("epever_energy_generated_today_watthours",
               energy_generated_today);
  }

  double energy_generated_total = 0;
  if (-1 ==
      read_input_register_double_scaled_by(ctx, REGISTER_ENERGY_GENERATED_TOTAL,
                                           &energy_generated_total, 0.1)) {
    fprintf(FD_ERROR, "Reading energy generated total failed\n");
  } else {
    add_metric("epever_energy_generated_total_watthours",
               energy_generated_total);
  }

  double battery_voltage = 0;
  if (-1 == read_input_register_scaled(ctx, REGISTER_BATTERY_VOLTAGE,
                                       &battery_voltage)) {
    fprintf(FD_ERROR, "Reading battery voltage failed\n");
  } else {
    add_metric("epever_battery_volts", battery_voltage);
  }

  double battery_current = 0;
  if (-1 == read_input_register_scaled(ctx, REGISTER_BATTERY_CURRENT,
                                       &battery_current)) {
    fprintf(FD_ERROR, "Reading battery current failed\n");
  } else {
    add_metric("epever_battery_amperes", battery_current);
  }

  double battery_power = 0;
  if (-1 == read_input_register_double_scaled(ctx, REGISTER_BATTERY_POWER,
                                              &battery_power)) {
    fprintf(FD_ERROR, "Reading battery power failed\n");
  } else {
    add_metric("epever_battery_watts", battery_power);
  }

  double battery_temperature = 0;
  if (-1 == read_input_register_scaled(ctx, REGISTER_BATTERY_TEMPERATURE,
                                       &battery_temperature)) {
    fprintf(FD_ERROR, "Reading battery temperature failed\n");
  } else {
    add_metric("epever_battery_temperature_celsius", battery_temperature);
  }

  double device_temperature = 0;
  if (-1 == read_input_register_scaled(ctx, REGISTER_DEVICE_TEMPERATURE,
                                       &device_temperature)) {
    fprintf(FD_ERROR, "Reading device temperature failed\n");
  } else {
    add_metric("epever_device_temperature_celsius", device_temperature);
  }

  double battery_soc = 0;
  if (-1 ==
      read_input_register_scaled(ctx, REGISTER_BATTERY_SOC, &battery_soc)) {
    fprintf(FD_ERROR, "Reading battery SOC failed\n");
  } else {
    add_metric("epever_battery_soc", battery_soc);
  }

  fprintf(FD_DEBUG, "last_time_read_settings_at[%" PRIu8 "] = %lf\n", id,
          difftime(now, last_time_read_settings_at[id]));
  if (difftime(now, last_time_read_settings_at[id]) > 1 * HOUR) {
    double rated_input_current = 0;
    if (-1 == read_input_register_scaled(ctx, REGISTER_RATED_INPUT_CURRENT,
                                         &rated_input_current)) {
      fprintf(FD_ERROR, "Reading rated input current failed\n");
    } else {
      add_metric("epever_rated_input_current", rated_input_current);
    }

    double boost_voltage = 0;
    if (-1 == read_holding_register_scaled(ctx, REGISTER_SETTINGS_BOOST_VOLTAGE,
                                           &boost_voltage)) {
      fprintf(FD_ERROR, "Reading boost voltage failed\n");
    } else {
      add_metric("epever_settings_boost_voltage", boost_voltage);
    }

    double float_voltage = 0;
    if (-1 == read_holding_register_scaled(ctx, REGISTER_SETTINGS_FLOAT_VOLTAGE,
                                           &float_voltage)) {
      fprintf(FD_ERROR, "Reading float voltage failed\n");
    } else {
      add_metric("epever_settings_float_voltage", float_voltage);
    }

    double boost_reconnect_voltage = 0;
    if (-1 == read_holding_register_scaled(
                  ctx, REGISTER_SETTINGS_BOOST_RECONNECT_VOLTAGE,
                  &boost_reconnect_voltage)) {
      fprintf(FD_ERROR, "Reading boost reconnect voltage failed\n");
    } else {
      add_metric("epever_settings_boost_voltage", boost_voltage);
    }

    uint16_t length_of_night_buffer = 0;
    if (-1 == modbus_read_registers(ctx, REGISTER_SETTINGS_LENGTH_OF_NIGHT, 1,
                                    &length_of_night_buffer)) {
      fprintf(FD_ERROR, "Reading length of night failed\n");
    } else {
      const double hour =
          (double)(length_of_night_buffer >> REGISTER_HALF_SIZE);
      const double minute =
          (double)(length_of_night_buffer & REGISTER_HALF_MASK);
      const double length_of_night = hour + (minute / 60.0);
      add_metric("epever_settings_length_of_night_hours", length_of_night);
    }

    // double foo = 0;
    // uint16_t buffer[3] = {0, 0, 0};
    // if (-1 == modbus_read_registers(ctx, 0x9065, 1, buffer)) {
    //  fprintf(FD_ERROR, "Reading foo failed\n");
    //}
    // fprintf(FD_ERROR, "buffer = 0x%X 0x%X 0x%X\n", buffer[0], buffer[1],
    //        buffer[2]);

    last_time_read_settings_at[id] = now; // FIXME
  }

  // double battery_min, battery_max;
  // read_input_register_scaled(ctx, 0x3303, &battery_min);
  // read_input_register_scaled(ctx, 0x3302, &battery_max);
  // printf("battery today: %.2f V min | %.2f V max\n", battery_min,
  // battery_max);

  // dest[strlen(dest)] = '\0';

  modbus_close(ctx);
  modbus_free(ctx);

  return EXIT_SUCCESS;
}

int query(char *dest, const uint8_t *ids) {
  *dest = '\0'; // empty buffer content from previous queries

  int count = 0;
  while (ids[++count])
    ;
  fprintf(FD_DEBUG, "Found %d device IDs to query\n", count);

  thrd_t threads[MAX_DEVICE_ID + 1];

  // char *device_metrics = malloc(count * PROMETHEUS_RESPONSE_SIZE *
  // sizeof(char)); thrd_t *threads = malloc(count * sizeof(thrd_t)); if
  // (!device_metrics || !threads) {
  //    fprintf(FD_ERROR, "Cannot allocate memory\n");
  //    return EXIT_FAILURE;
  //}

  for (int i = 0; i < count; i++) {
    int status = thrd_create(&threads[i], (thrd_start_t)query_device_thread,
                             (void *)(&(ids[i])));
    if (status != thrd_success) {
      fprintf(FD_ERROR, "thrd_create() failed\n");
      return EXIT_FAILURE;
    }
  }

  for (int i = 0; i < count; i++) {
    if (thrd_join(threads[i], NULL) != thrd_success) {
      fprintf(FD_ERROR, "Thread %d failed\n", i);
      // return EXIT_FAILURE;
    } else {
      const uint8_t id = ids[i];
      const char *metrics = device_metrics[id];
      // fprintf(FD_DEBUG, "Metrics from device ID %" PRIu8 ":\n%s\n\n", id,
      // metrics);
      fprintf(FD_DEBUG, "Got metrics from device ID %" PRIu8 "\n", id);
      fprintf(FD_DEBUG, "length = %lu\n", strlen(metrics));
      strcat(dest, metrics);
    }
  }
  //  strlcat(dest, device_metrics, PROMETHEUS_RESPONSE_SIZE);

  return EXIT_SUCCESS;
}
