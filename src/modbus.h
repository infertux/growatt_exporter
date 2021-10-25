#include <bsd/string.h>
#include <errno.h>
#include <modbus-rtu.h>
#include <stdio.h>

#define DEBUG FALSE
#define MODBUS_TIMEOUT_ERROR 110

#define REGISTER_SIZE 16U

#define REGISTER_PV_VOLTAGE 0x3100
#define REGISTER_PV_CURRENT 0x3101
#define REGISTER_PV_POWER 0x3102

#define REGISTER_BATTERY_VOLTAGE 0x3104
#define REGISTER_BATTERY_CURRENT 0x3105
#define REGISTER_BATTERY_POWER 0x3106
#define REGISTER_BATTERY_TEMPERATURE 0x3110
#define REGISTER_BATTERY_SOC 0x311A

#define REGISTER_BATTERY_STATUS 0x3200
#define REGISTER_CHARGING_STATUS 0x3201

#define REGISTER_ENERGY_GENERATED_TODAY 0x330C

#define metric(name, value)                                                    \
  do {                                                                         \
    char buffer[256];                                                          \
    snprintf(buffer, sizeof(buffer), "# TYPE %s gauge\n%s%s %lf\n", name,      \
             name, labels, value);                                             \
    strlcat(dest, buffer, 256);                                                \
  } while (0)

int read_register_raw(modbus_t *ctx, const int addr, int size,
                      uint16_t *buffer) {
  if (modbus_read_input_registers(ctx, addr, size, buffer) == -1) {
    if (errno != MODBUS_TIMEOUT_ERROR) {
      fprintf(stderr, "%s (%d)\n", modbus_strerror(errno), errno);
      return -1;
    }

    // try again if it timed out
    if (modbus_read_input_registers(ctx, addr, 1, buffer) == -1) {
      fprintf(stderr, "%s (%d)\n", modbus_strerror(errno), errno);
      return -1;
    }
  }

  return 0;
}

int read_register(modbus_t *ctx, const int addr, double *value) {
  uint16_t buffer[1] = {0};
  int ret = read_register_raw(ctx, addr, 1, buffer);
  if (ret) {
    return ret;
  }

  *value = (double)buffer[0];

  return 0;
}

int read_register_scaled_by(modbus_t *ctx, const int addr, double *value,
                            double scale) {
  uint16_t buffer[1] = {0};
  int ret = read_register_raw(ctx, addr, 1, buffer);
  if (ret) {
    return ret;
  }

  *value = buffer[0] / scale;

  return 0;
}

int read_register_scaled(modbus_t *ctx, const int addr, double *value) {
  return read_register_scaled_by(ctx, addr, value, 100.0);
}

int read_register_double_scaled_by(modbus_t *ctx, const int addr, double *value,
                                   double scale) {
  uint16_t buffer[2] = {0, 0};
  int ret = read_register_raw(ctx, addr, 2, buffer);
  if (ret) {
    return ret;
  }

  *value = ((double)(buffer[1] << REGISTER_SIZE) + (double)(buffer[0])) / scale;
  // if (*value > 4194304.0) { // 2^22
  //     /* XXX: sometimes we get insanely big numbers usually followed by
  //     "Invalid
  //      * data (112345691)" error on subsequant polls, it seems to fix itself
  //      * after a few minutes, anything above 2^22 is probaly garbage data so
  //      we
  //      * ignore it */
  //     return -1;
  // }

  return 0;
}

int read_register_double_scaled(modbus_t *ctx, const int addr, double *value) {
  return read_register_double_scaled_by(ctx, addr, value, 100.0);
}

int bye(modbus_t *ctx, char *error) {
  fprintf(stderr, "%s: %s (%d)\n", error, modbus_strerror(errno), errno);
  if (ctx) {
    modbus_free(ctx);
  }
  return 1;
}

int query_device(const int id, char *dest) {
  fprintf(stderr, "Querying device ID %d...\n", id);

  char labels[32];
  snprintf(labels, sizeof(labels), "{device_id=\"%d\"}", id);

  modbus_t *ctx = NULL;
  // XXX: ideally we could use TCP but this always times out for some reason:
  // ctx = modbus_new_tcp("192.168.1.X", 8088);
  // ... so we use socat:
  // socat -ls -v pty,link=/tmp/ttyepever123 tcp:192.168.1.X:8088
  char path[32];
  snprintf(path, sizeof(path), "/tmp/ttyepever%d", id);
  ctx = modbus_new_rtu(path, 115200, 'N', 8, 1);
  if (ctx == NULL) {
    return bye(ctx, "Unable to create the libmodbus context");
  }

  if (modbus_set_debug(ctx, DEBUG)) {
    return bye(ctx, "Set debug flag failed");
  }

  if (modbus_set_slave(ctx, 1)) { // required with RTU mode
    return bye(ctx, "Set slave failed");
  }

  modbus_set_response_timeout(ctx, 5, 0); // 5 seconds

  if (modbus_connect(ctx)) {
    return bye(ctx, "Connection failed");
  }

  // TODO: sync time from PC during first minute of each hour

  double battery_status = 0;
  if (read_register(ctx, REGISTER_BATTERY_STATUS, &battery_status)) {
    return bye(ctx, "Reading battery status failed");
  }
  metric("epever_battery_status", battery_status);

  double charging_status = 0;
  if (read_register(ctx, REGISTER_CHARGING_STATUS, &charging_status)) {
    return bye(ctx, "Reading charging status failed");
  }
  metric("epever_charging_status", charging_status);

  // double rated_current;
  // if (read_register_scaled(ctx, 0x3001, &rated_current)) {
  //  return bye(ctx, "Reading rated_current failed");
  //}
  // printf("rated_current = %.0f A\n", rated_current);

  double pv_voltage = 0;
  if (read_register_scaled(ctx, REGISTER_PV_VOLTAGE, &pv_voltage)) {
    return bye(ctx, "Reading PV voltage failed");
  }
  metric("epever_pv_volts", pv_voltage);

  double pv_current = 0;
  if (read_register_scaled(ctx, REGISTER_PV_CURRENT, &pv_current)) {
    return bye(ctx, "Reading PV current failed");
  }
  metric("epever_pv_amperes", pv_current);

  double pv_power = 0;
  if (read_register_double_scaled(ctx, REGISTER_PV_POWER, &pv_power)) {
    return bye(ctx, "Reading PV power failed");
  }
  metric("epever_pv_watts", pv_power);

  double energy_generated_today = 0;
  if (read_register_double_scaled_by(ctx, REGISTER_ENERGY_GENERATED_TODAY,
                                     &energy_generated_today, 0.1)) {
    return bye(ctx, "Reading energy generated today failed");
  }
  metric("epever_energy_generated_today_watthours", energy_generated_today);

  double battery_voltage = 0;
  if (read_register_scaled(ctx, REGISTER_BATTERY_VOLTAGE, &battery_voltage)) {
    return bye(ctx, "Reading battery voltage failed");
  }
  metric("epever_battery_volts", battery_voltage);

  double battery_current = 0;
  if (read_register_scaled(ctx, REGISTER_BATTERY_CURRENT, &battery_current)) {
    return bye(ctx, "Reading battery current failed");
  }
  metric("epever_battery_amperes", battery_current);

  double battery_power = 0;
  if (read_register_double_scaled(ctx, REGISTER_BATTERY_POWER,
                                  &battery_power)) {
    return bye(ctx, "Reading battery power failed");
  }
  metric("epever_battery_watts", battery_power);

  double battery_temperature = 0;
  if (read_register_scaled(ctx, REGISTER_BATTERY_TEMPERATURE,
                           &battery_temperature)) {
    return bye(ctx, "Reading battery temperature failed");
  }
  metric("epever_battery_temperature_celsius", battery_temperature);

  double battery_soc = 0;
  if (read_register_scaled(ctx, REGISTER_BATTERY_SOC, &battery_soc)) {
    return bye(ctx, "Reading battery SOC failed");
  }
  metric("epever_battery_soc", battery_soc);

  // double battery_min, battery_max;
  // read_register_scaled(ctx, 0x3303, &battery_min);
  // read_register_scaled(ctx, 0x3302, &battery_max);
  // printf("battery today: %.2f V min | %.2f V max\n", battery_min,
  // battery_max);

  modbus_close(ctx);
  modbus_free(ctx);

  return 0;
}

int query(const int *ids, char *dest) {
  *dest = '\0'; // make sure buffer is clean

  for (int i = 0; ids[i] >= 0; i++) {
    int ret = query_device(ids[i], dest);
    if (ret) {
      return ret;
    }
  }

  return 0;
}
