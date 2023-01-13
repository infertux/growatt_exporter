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

#define TIMEZONE_BIAS (7 * HOURS)
#define MAX_DEVICE_ID 100

#define HOUR 3600
#define HOURS HOUR

#define DEBUG FALSE

#define PROMETHEUS_RESPONSE_SIZE 8192U
#define PROMETHEUS_METRIC_SIZE 256U

#define MODBUS_BAUD 9600
#define MODBUS_PARITY 'N'
#define MODBUS_DATA_BIT 8
#define MODBUS_STOP_BIT 1

#define MODBUS_RESPONSE_TIMEOUT 3

#define REGISTER_SIZE 16U
#define REGISTER_HALF_SIZE (REGISTER_SIZE / 2)
#define REGISTER_HALF_MASK 0xFFU

#define REGISTER_SYSTEM_STATUS 0
#define REGISTER_PV_VOLTAGE 1
#define REGISTER_PV_POWER 3
#define REGISTER_BATTERY_VOLTAGE 17
#define REGISTER_BATTERY_SOC 18
#define REGISTER_TEMPERATURE_INVERTER 25
#define REGISTER_TEMPERATURE_DCDC 26
#define REGISTER_FAN_SPEED_MPPT 81
#define REGISTER_FAN_SPEED_INVERTER 82

#define CLOCK_OFFSET_THRESHOLD 30 // seconds

#define add_metric(name, value)                                                \
  do {                                                                         \
    char buffer[PROMETHEUS_METRIC_SIZE];                                       \
    snprintf(buffer, sizeof(buffer),                                           \
             "# TYPE growatt_%s gauge\ngrowatt_%s{%s} %lf\n", name, name,      \
             device_id_label, value);                                          \
                                                                               \
    const uint16_t len = strlcat(dest, buffer, PROMETHEUS_RESPONSE_SIZE);      \
    if (len >= PROMETHEUS_RESPONSE_SIZE) {                                     \
      return query_device_failed(ctx, id, "buffer overflow");                  \
    }                                                                          \
                                                                               \
    if (strcmp(name, "read_metric_failed_total") != 0 &&                       \
        strcmp(name, "read_metric_succeeded_total") != 0) {                    \
      read_metric_succeeded_total[id]++;                                       \
    }                                                                          \
  } while (0)

char device_metrics[MAX_DEVICE_ID + 1]
                   [PROMETHEUS_RESPONSE_SIZE * sizeof(char)] = {{'\0'}};

uint8_t read_metric_failed_total[MAX_DEVICE_ID + 1] = {0};
uint8_t read_metric_succeeded_total[MAX_DEVICE_ID + 1] = {0};

time_t last_time_synced_at[MAX_DEVICE_ID + 1] = {0};
time_t last_time_read_settings_at[MAX_DEVICE_ID + 1] = {0};

#define read_input_registers modbus_read_input_registers
#define read_holding_registers modbus_read_registers
#define write_holding_registers modbus_write_registers

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
  *value *= scale;

  return ret;
}

int read_holding_register_scaled_by(modbus_t *ctx, const int addr,
                                    double *value, double scale) {
  int ret = read_holding_register(ctx, addr, value);
  *value *= scale;

  return ret;
}

int read_input_register_scaled(modbus_t *ctx, const int addr, double *value) {
  return read_input_register_scaled_by(ctx, addr, value, 1.0 / 10.0);
}

int read_holding_register_scaled(modbus_t *ctx, const int addr, double *value) {
  return read_holding_register_scaled_by(ctx, addr, value, 1.0 / 10.0);
}

int read_input_register_double_scaled_by(modbus_t *ctx, const int addr,
                                         double *value, double scale) {
  uint16_t buffer[2] = {0, 0};
  int ret = read_input_registers(ctx, addr, 2, buffer);
  *value = ((double)(buffer[0] << REGISTER_SIZE) + (double)(buffer[1])) * scale;

  return ret;
}

int read_input_register_double_scaled(modbus_t *ctx, const int addr,
                                      double *value) {
  return read_input_register_double_scaled_by(ctx, addr, value, 1.0 / 10.0);
}

/*
void clock_write(modbus_t *ctx) {
  const time_t now = time(NULL) + TIMEZONE_BIAS +
                     2; // adding 2 seconds because writing registers is slow so
                        // we need to compensate for it
  const struct tm *tm = gmtime(&now);
  const uint16_t year_offset = 100;

  const uint16_t clock[3] = {
      ((uint16_t)tm->tm_min << REGISTER_HALF_SIZE) + (uint8_t)tm->tm_sec,
      ((uint16_t)tm->tm_mday << REGISTER_HALF_SIZE) + (uint8_t)tm->tm_hour,
      (((uint16_t)tm->tm_year - year_offset) << REGISTER_HALF_SIZE) +
          (uint8_t)tm->tm_mon + 1,
  };

  fprintf(LOG_DEBUG, "About to write %04X·%04X·%04X into clock register\n",
          clock[2], clock[1], clock[0]);

  if (3 != write_holding_registers(ctx, REGISTER_CLOCK, 3, clock)) {
    fprintf(LOG_ERROR, "Writing clock failed\n");
  } else {
    fprintf(LOG_INFO, "Writing clock succeeded\n");
  }
}

int clock_sync(modbus_t *ctx) {
  uint16_t clock[3] = {0, 0, 0};
  if (-1 == read_holding_registers(ctx, REGISTER_CLOCK, 3, clock)) {
    fprintf(LOG_ERROR, "Reading clock failed\n");
    return INT_MAX;
  }

  fprintf(LOG_DEBUG, "Clock register is %04X·%04X·%04X\n", clock[2], clock[1],
          clock[0]);

  const int year_offset = 100;
  struct tm clock_tm = {
      (int)(clock[0] & REGISTER_HALF_MASK),           // seconds
      (clock[0] >> REGISTER_HALF_SIZE),               // minutes
      (int)(clock[1] & REGISTER_HALF_MASK),           // hours
      (clock[1] >> REGISTER_HALF_SIZE),               // day
      (int)(clock[2] & REGISTER_HALF_MASK) - 1,       // month
      (clock[2] >> REGISTER_HALF_SIZE) + year_offset, // year;
  };
  const time_t clock_time_t = mktime(&clock_tm);
  const time_t now = time(NULL) + TIMEZONE_BIAS;
  const double difference = difftime(clock_time_t, now);

  if (fabs(difference) >= CLOCK_OFFSET_THRESHOLD) {
    fprintf(LOG_ERROR, "Device time is %.0lfs ahead!\n", difference);

    char time_string[sizeof "2011-10-08T07:07:09Z"];
    strftime(time_string, sizeof time_string, "%FT%T", &clock_tm);
    fprintf(LOG_ERROR, "device time = %s\n", time_string);

    const struct tm *now_tm = gmtime(&now);
    strftime(time_string, sizeof time_string, "%FT%T", now_tm);
    fprintf(LOG_ERROR, "        now = %s\n", time_string);

    clock_write(ctx);

    return (int)difference;
  } else {
    fprintf(LOG_INFO, "Device time is %.0lfs ahead\n", difference);
  }

  return EXIT_SUCCESS;
}
*/

int query_device_failed(modbus_t *ctx, const uint8_t id, const char *message) {
  if (errno) {
    fprintf(LOG_ERROR, "[Device %" PRIu8 "] %s: %s (%d)\n", id, message,
            modbus_strerror(errno), errno);
  }

  if (ctx) {
    modbus_free(ctx);
  }

  return (errno ? errno : 9001);
}

void read_register_failed(const uint8_t id, const char *message) {
  read_metric_failed_total[id]++;

  fprintf(LOG_ERROR, "[Device %" PRIu8 "] Reading register %s failed", id,
          message);

  if (errno) {
    fprintf(LOG_ERROR, ": %s (%d)", modbus_strerror(errno), errno);
  }

  fprintf(LOG_ERROR, "\n");
}

int query_device_thread(void *id_ptr) {
  const uint8_t id = *((uint8_t *)id_ptr);
  fprintf(LOG_DEBUG, "Querying device ID %" PRIu8 " in thread ID %lu...\n", id,
          thrd_current());

  read_metric_failed_total[id] = 0;
  read_metric_succeeded_total[id] = 0;

  char *dest = device_metrics[id];
  *dest = '\0'; // empty buffer content from previous queries

  char device_id_label[32];
  snprintf(device_id_label, sizeof(device_id_label), "device_id=\"%" PRIu8 "\"",
           id);

  modbus_t *ctx = NULL;
  ctx = modbus_new_rtu("/dev/ttyUSB0", MODBUS_BAUD, MODBUS_PARITY,
                       MODBUS_DATA_BIT, MODBUS_STOP_BIT);
  if (ctx == NULL) {
    return query_device_failed(ctx, id,
                               "Unable to create the libmodbus context");
  }

  if (modbus_set_debug(ctx, DEBUG)) {
    return query_device_failed(ctx, id, "Set debug flag failed");
  }

  if (modbus_set_slave(ctx, 1)) { // required with RTU mode
    return query_device_failed(ctx, id, "Set slave failed");
  }

  modbus_set_response_timeout(ctx, MODBUS_RESPONSE_TIMEOUT, 0); // in seconds

  if (modbus_connect(ctx)) {
    return query_device_failed(ctx, id, "Connection failed");
  }

  /*
  const time_t now = time(NULL);

  fprintf(LOG_DEBUG, "last_time_synced_at[%" PRIu8 "] = %lf\n", id,
          difftime(now, last_time_synced_at[id]));
  if (difftime(now, last_time_synced_at[id]) > 24 * HOURS) {
    if (clock_sync(ctx)) {
      fprintf(LOG_ERROR, "Synced time\n");
    }

    last_time_synced_at[id] = now;
  }
  */

  double system_status = 0;
  if (-1 == read_input_register(ctx, REGISTER_SYSTEM_STATUS, &system_status)) {
    read_register_failed(id, "system status");
  } else {
    add_metric("system_status", system_status);
  }

  double pv_voltage = 0;
  if (-1 == read_input_register_scaled(ctx, REGISTER_PV_VOLTAGE, &pv_voltage)) {
    read_register_failed(id, "PV voltage");
  } else {
    add_metric("pv_volts", pv_voltage);
  }

  /*
  double pv_current = 0;
  if (-1 == read_input_register_scaled(ctx, REGISTER_PV_CURRENT, &pv_current)) {
    read_register_failed(id, "PV current");
  } else {
    add_metric("pv_amperes", pv_current);
  }
  */

  double pv_power = 0;
  if (-1 ==
      read_input_register_double_scaled(ctx, REGISTER_PV_POWER, &pv_power)) {
    read_register_failed(id, "PV power");
  } else {
    add_metric("pv_watts", pv_power);
  }

  /*
  double energy_generated_today = 0;
  if (-1 ==
      read_input_register_double_scaled_by(
          ctx, REGISTER_ENERGY_GENERATED_TODAY, &energy_generated_today,
          // NOLINTNEXTLINE(cppcoreguidelines-avoid-magic-numbers,readability-magic-numbers)
          1000.0 / 100.0)) {
    read_register_failed(id, "energy generated today");
  } else {
    add_metric("energy_generated_today_watthours", energy_generated_today);
  }

  double energy_generated_total = 0;
  if (-1 ==
      read_input_register_double_scaled_by(
          ctx, REGISTER_ENERGY_GENERATED_TOTAL, &energy_generated_total,
          // NOLINTNEXTLINE(cppcoreguidelines-avoid-magic-numbers,readability-magic-numbers)
          1000.0 / 100.0)) {
    read_register_failed(id, "energy generated total");
  } else {
    add_metric("energy_generated_total_watthours", energy_generated_total);
  }
  */

  double battery_voltage = 0;
  if (-1 == read_input_register_scaled_by(ctx, REGISTER_BATTERY_VOLTAGE,
                                          &battery_voltage, 1.0 / 100.0)) {
    read_register_failed(id, "battery voltage");
  } else {
    add_metric("battery_volts", battery_voltage);
  }

  /*
  double battery_current = 0;
  if (-1 == read_input_register_scaled(ctx, REGISTER_BATTERY_CURRENT,
                                       &battery_current)) {
    read_register_failed(id, "battery current");
  } else {
    add_metric("battery_amperes", battery_current);
  }

  double battery_power = 0;
  if (-1 == read_input_register_double_scaled(ctx, REGISTER_BATTERY_POWER,
                                              &battery_power)) {
    read_register_failed(id, "battery power");
  } else {
    add_metric("battery_watts", battery_power);
  }
  */

  double battery_soc = 0;
  if (-1 == read_input_register(ctx, REGISTER_BATTERY_SOC, &battery_soc)) {
    read_register_failed(id, "battery SOC");
  } else {
    add_metric("battery_soc", battery_soc);
  }

  double temperature_inverter = 0;
  if (-1 == read_input_register_scaled(ctx, REGISTER_TEMPERATURE_INVERTER,
                                       &temperature_inverter)) {
    read_register_failed(id, "inverter temperature");
  } else {
    add_metric("temperature_inverter_celsius", temperature_inverter);
  }

  double temperature_dcdc = 0;
  if (-1 == read_input_register_scaled(ctx, REGISTER_TEMPERATURE_DCDC,
                                       &temperature_dcdc)) {
    read_register_failed(id, "DC-DC temperature");
  } else {
    add_metric("temperature_dcdc_celsius", temperature_dcdc);
  }

  double fan_speed_mppt = 0;
  if (-1 == read_input_register(ctx, REGISTER_FAN_SPEED_MPPT, &fan_speed_mppt)) {
    read_register_failed(id, "fan speed MPPT");
  } else {
    add_metric("fan_speed_mppt", fan_speed_mppt);
  }

  double fan_speed_inverter = 0;
  if (-1 == read_input_register(ctx, REGISTER_FAN_SPEED_INVERTER, &fan_speed_inverter)) {
    read_register_failed(id, "fan speed inverter");
  } else {
    add_metric("fan_speed_inverter", fan_speed_inverter);
  }

  /*
  fprintf(LOG_DEBUG, "last_time_read_settings_at[%" PRIu8 "] = %lf\n", id,
          difftime(now, last_time_read_settings_at[id]));
  if (difftime(now, last_time_read_settings_at[id]) > 1 * HOUR) {
    double rated_input_current = 0;
    if (-1 == read_input_register_scaled(ctx, REGISTER_RATED_INPUT_CURRENT,
                                         &rated_input_current)) {
      read_register_failed(id, "rated input current");
    } else {
      add_metric("rated_input_current", rated_input_current);
    }

    double charging_limit_voltage = 0;
    if (-1 == read_holding_register_scaled(
                  ctx, REGISTER_SETTINGS_CHARGING_LIMIT_VOLTAGE,
                  &charging_limit_voltage)) {
      read_register_failed(id, "charging limit voltage");
    } else {
      add_metric("settings_charging_limit_voltage", charging_limit_voltage);
    }

    double boost_voltage = 0;
    if (-1 == read_holding_register_scaled(ctx, REGISTER_SETTINGS_BOOST_VOLTAGE,
                                           &boost_voltage)) {
      read_register_failed(id, "boost voltage");
    } else {
      add_metric("settings_boost_voltage", boost_voltage);
    }

    double float_voltage = 0;
    if (-1 == read_holding_register_scaled(ctx, REGISTER_SETTINGS_FLOAT_VOLTAGE,
                                           &float_voltage)) {
      read_register_failed(id, "float voltage");
    } else {
      add_metric("settings_float_voltage", float_voltage);
    }

    double boost_reconnect_voltage = 0;
    if (-1 == read_holding_register_scaled(
                  ctx, REGISTER_SETTINGS_BOOST_RECONNECT_VOLTAGE,
                  &boost_reconnect_voltage)) {
      read_register_failed(id, "boost reconnect voltage");
    } else {
      add_metric("settings_boost_voltage", boost_voltage);
    }

    double boost_duration = 0;
    if (-1 ==
        read_holding_register_scaled_by(
            ctx, REGISTER_SETTINGS_BOOST_DURATION, &boost_duration,
            // NOLINTNEXTLINE(cppcoreguidelines-avoid-magic-numbers,readability-magic-numbers)
            60.0)) {
      read_register_failed(id, "boost duration");
    } else {
      add_metric("settings_boost_duration_seconds", boost_duration);
    }

    uint16_t length_of_night_buffer = 0;
    if (-1 == read_holding_registers(ctx, REGISTER_SETTINGS_LENGTH_OF_NIGHT, 1,
                                     &length_of_night_buffer)) {
      read_register_failed(id, "length of night");
    } else {
      const double hour =
          (double)(length_of_night_buffer >> REGISTER_HALF_SIZE);
      const double minute =
          (double)(length_of_night_buffer & REGISTER_HALF_MASK);
      const double length_of_night = hour + (minute / 60.0);
      add_metric("settings_length_of_night_hours", length_of_night);
    }

    last_time_read_settings_at[id] = now; // FIXME
  }
  */

  add_metric("read_metric_failed_total", (double)read_metric_failed_total[id]);
  add_metric("read_metric_succeeded_total",
             (double)read_metric_succeeded_total[id]);

  modbus_close(ctx);
  modbus_free(ctx);

  return EXIT_SUCCESS;
}

int query(char *dest, const uint8_t *ids) {
  *dest = '\0'; // empty buffer content from previous queries

  int count = 0;
  while (ids[++count]) // NOLINT: counting ids until we get a zero value
    ;
  fprintf(LOG_DEBUG, "Found %d device IDs to query\n", count);

  thrd_t threads[MAX_DEVICE_ID + 1];

  for (int i = 0; i < count; i++) {
    int status = thrd_create(&threads[i], (thrd_start_t)query_device_thread,
                             (void *)(&(ids[i])));
    if (status != thrd_success) {
      fprintf(LOG_ERROR, "thrd_create() failed\n");
      return EXIT_FAILURE;
    }
  }

  for (int i = 0; i < count; i++) {
    int result = 0;
    if (thrd_join(threads[i], &result) != thrd_success) {
      fprintf(LOG_ERROR, "Thread %d failed (code = %d)\n", i, result);
      return EXIT_FAILURE;
    } else {
      fprintf(LOG_DEBUG, "Thread %d succeeded (code = %d)\n", i, result);
      const uint8_t id = ids[i];
      const char *metrics = device_metrics[id];
      fprintf(LOG_DEBUG, "Got metrics from device ID %" PRIu8 " (%zu bytes)\n",
              id, strlen(metrics));

      const uint16_t len = strlcat(dest, metrics, PROMETHEUS_RESPONSE_SIZE);
      if (len >= PROMETHEUS_RESPONSE_SIZE) {
        fprintf(LOG_ERROR, "buffer overflow: %" PRIu16 " >= %i\n", len,
                PROMETHEUS_RESPONSE_SIZE);

        return ENAMETOOLONG;
      }
    }
  }
  return EXIT_SUCCESS;
}
