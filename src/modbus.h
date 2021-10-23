#include <stdio.h>
#include <stdlib.h>
#include <string.h> // strcat()
#include <errno.h>
#include <modbus-rtu.h>

#define DEBUG FALSE
#define MODBUS_TIMEOUT_ERROR 110

#define metric(name, value) \
  do { \
    char buffer[256]; \
    sprintf(buffer, "# TYPE %s gauge\n%s%s %lf\n", name, name, labels, value); \
    strcat(dest, buffer); \
  } while (0)


int read_register_raw(modbus_t *ctx, const int addr, int size, uint16_t *buffer) {
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
  uint16_t buffer[1] = { 0 };
  int ret = read_register_raw(ctx, addr, 1, buffer);
  if (ret) return ret;

  *value = (double)buffer[0];

  return 0;
}

int read_register_scaled_by(modbus_t *ctx, const int addr, double *value, double scale) {
  uint16_t buffer[1] = { 0 };
  int ret = read_register_raw(ctx, addr, 1, buffer);
  if (ret) return ret;

  *value = buffer[0] / scale;

  return 0;
}

int read_register_scaled(modbus_t *ctx, const int addr, double *value) {
    return read_register_scaled_by(ctx, addr, value, 100.0);
}

int read_register_double_scaled_by(modbus_t *ctx, const int addr, double *value, double scale) {
  uint16_t buffer[2] = { 0, 0 };
  int ret = read_register_raw(ctx, addr, 2, buffer);
  if (ret) return ret;

  *value = ((double)(buffer[1] << 16) + (double)(buffer[0])) / scale;
  // if (*value > 4194304.0) { // 2^22
  //     /* XXX: sometimes we get insanely big numbers usually followed by "Invalid
  //      * data (112345691)" error on subsequant polls, it seems to fix itself
  //      * after a few minutes, anything above 2^22 is probaly garbage data so we
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
  if (ctx != NULL) modbus_free(ctx);
  return 1;
}

int query_device(const int id, char *dest) {
  fprintf(stderr, "Querying device ID %d...\n", id);

  char labels[32];
  sprintf(labels, "{device_id=\"%d\"}", id);

  modbus_t *ctx;
  // XXX: ideally we could use TCP but this always times out for some reason:
  // ctx = modbus_new_tcp("192.168.1.X", 8088);
  // ... so we use socat:
  // socat -ls -v pty,link=/tmp/ttyepever123 tcp:192.168.1.X:8088
  char path[32];
  sprintf(path, "/tmp/ttyepever%d", id);
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

  modbus_set_response_timeout(ctx, 1, 0); // 1 second

  if (modbus_connect(ctx)) {
    return bye(ctx, "Connection failed");
  }

  // TODO: sync time from PC

  double battery_status;
  if (read_register(ctx, 0x3200, &battery_status)) {
    return bye(ctx, "Reading battery status failed");
  }
  metric("epever_battery_status", battery_status);

  double charging_status;
  if (read_register(ctx, 0x3201, &charging_status)) {
    return bye(ctx, "Reading charging status failed");
  }
  metric("epever_charging_status", charging_status);

  //double rated_current;
  //if (read_register_scaled(ctx, 0x3001, &rated_current)) {
  //  return bye(ctx, "Reading rated_current failed");
  //}
  // printf("rated_current = %.0f A\n", rated_current);

  double pv_voltage, pv_current, pv_power;
  if (read_register_scaled(ctx, 0x3100, &pv_voltage)) {
    return bye(ctx, "Reading PV voltage failed");
  }
  metric("epever_pv_volts", pv_voltage);
  if (read_register_scaled(ctx, 0x3101, &pv_current)) {
    return bye(ctx, "Reading PV current failed");
  }
  metric("epever_pv_amperes", pv_current);
  if (read_register_double_scaled(ctx, 0x3102, &pv_power)) {
    return bye(ctx, "Reading PV power failed");
  }
  metric("epever_pv_watts", pv_power);

  double generated_energy_today;
  if (read_register_double_scaled_by(ctx, 0x330C, &generated_energy_today, 0.1)) {
      return bye(ctx, "Reading generated energy today failed");
  }
  metric("epever_generated_energy_today_watthours", generated_energy_today);

  double battery_voltage, battery_current, battery_power, battery_temperature, battery_soc;
  if (read_register_scaled(ctx, 0x3104, &battery_voltage)) {
      return bye(ctx, "Reading battery voltage failed");
  }
  metric("epever_battery_volts", battery_voltage);
  if (read_register_scaled(ctx, 0x3105, &battery_current)) {
      return bye(ctx, "Reading battery current failed");
  }
  metric("epever_battery_amperes", battery_current);
  if (read_register_double_scaled(ctx, 0x3106, &battery_power)) {
      return bye(ctx, "Reading battery power failed");
  }
  metric("epever_battery_watts", battery_power);
  if (read_register_scaled(ctx, 0x3110, &battery_temperature)) {
      return bye(ctx, "Reading battery temperature failed");
  }
  metric("epever_battery_temperature_celsius", battery_temperature);
  if (read_register_scaled(ctx, 0x311A, &battery_soc)) {
      return bye(ctx, "Reading battery SOC failed");
  }
  metric("epever_battery_soc", battery_soc);

  //double battery_min, battery_max;
  //read_register_scaled(ctx, 0x3303, &battery_min);
  //read_register_scaled(ctx, 0x3302, &battery_max);
  //printf("battery today: %.2f V min | %.2f V max\n", battery_min, battery_max);

  modbus_close(ctx);
  modbus_free(ctx);

  return 0;
}

int query(const int *ids, char *dest) {
  *dest = '\0'; // make sure buffer is clean

  int ret;
  for (int i = 0; ids[i] >= 0; i++) {
      ret = query_device(ids[i], dest);
      if (ret) return ret;
  }

  return 0;
}

