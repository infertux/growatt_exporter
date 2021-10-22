#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <modbus-rtu.h>

#define DEBUG FALSE
#define TIMEOUT 110

int read_register(modbus_t *ctx, const int addr, float *value) {
  uint16_t buffer;

  if (modbus_read_input_registers(ctx, addr, 1, &buffer) == -1) {
    if (errno != TIMEOUT) {
      fprintf(stderr, "%s (%d)\n", modbus_strerror(errno), errno);
      return -1;
    }

    // try again if it timed out
    if (modbus_read_input_registers(ctx, addr, 1, &buffer) == -1) {
      fprintf(stderr, "%s (%d)\n", modbus_strerror(errno), errno);
      return -1;
    }
  }

  *value = buffer / 100.0;

  return 0;
}

int read_register_double(modbus_t *ctx, const int addr, float *value) {
  uint16_t tab_reg[2];

  if (modbus_read_input_registers(ctx, addr, 2, tab_reg) == -1) {
    fprintf(stderr, "%s\n", modbus_strerror(errno));
    return -1;
  }

  *value = (tab_reg[0] + (tab_reg[1] << 16)) / 100.0;

  return 0;
}

void metric(const char *name, float value) {
    printf("# HELP %s\n# TYPE %s gauge\n%s %f\n", name, name, name, value);
}

void bye(modbus_t *ctx, char *error) {
  fprintf(stderr, "%s: %s (%d)\n", error, modbus_strerror(errno), errno);
  if (ctx != NULL) modbus_free(ctx);
  exit(1);
}

int main() {
  modbus_t *ctx;
  uint16_t tab_reg[64];
  int rc;

  //ctx = modbus_new_tcp("192.168.1.41", 502);
  //socat -ls -v pty,link=/home/infertux/ttyV0 tcp:192.168.1.41:502
  ctx = modbus_new_rtu("/tmp/ttyepever", 115200, 'N', 8, 1);
  if (ctx == NULL) {
    bye(ctx, "Unable to create the libmodbus context");
  }

  if (modbus_set_debug(ctx, DEBUG)) {
    bye(ctx, "Set debug flag failed");
  }

  if (modbus_set_slave(ctx, 1)) { // required with RTU mode
    bye(ctx, "Set slave failed");
  }

  /* Save original timeout */
  //uint32_t old_response_to_sec;
  //uint32_t old_response_to_usec;
  //modbus_get_response_timeout(ctx, &old_response_to_sec, &old_response_to_usec);
  //printf("%d %d\n", old_response_to_sec, old_response_to_usec);
  modbus_set_response_timeout(ctx, 1, 0);
  //modbus_get_response_timeout(ctx, &old_response_to_sec, &old_response_to_usec);
  //printf("%d %d\n", old_response_to_sec, old_response_to_usec);
  // printf("timeout ret %d\n", modbus_set_byte_timeout(ctx, 10, 0));

  if (modbus_connect(ctx)) {
    bye(ctx, "Connection failed");
  }

  // TODO: sync time from PC

  //float rated_current;
  //if (read_register(ctx, 0x3001, &rated_current)) {
  //  bye(ctx, "Reading rated_current failed");
  //}
  // printf("rated_current = %.0f A\n", rated_current);

  float pv_voltage, pv_current, pv_power;
  if (read_register(ctx, 0x3100, &pv_voltage)) {
    bye(ctx, "Reading PV voltage failed");
  }
  metric("epever_pv_volts", pv_voltage);
  if (read_register(ctx, 0x3101, &pv_current)) {
    bye(ctx, "Reading PV current failed");
  }
  metric("epever_pv_amperes", pv_current);
  if (read_register_double(ctx, 0x3102, &pv_power)) {
    bye(ctx, "Reading PV power failed");
  }
  metric("epever_pv_watts", pv_power);

  float generated_energy_today;
  if (read_register_double(ctx, 0x330C, &generated_energy_today)) {
      bye(ctx, "Reading generated energy today failed");
  }
  //printf("generated today = %.0f Wh\n", generated_energy_today*1000.0);
  metric("epever_generated_energy_today_watthours", generated_energy_today*1000.0);

  float battery_voltage, battery_temperature, battery_soc;
  if (read_register(ctx, 0x3104, &battery_voltage)) {
      bye(ctx, "Reading battery voltage failed");
  }
  metric("epever_battery_volts", battery_voltage);
  if (read_register(ctx, 0x3110, &battery_temperature)) {
      bye(ctx, "Reading battery temperature failed");
  }
  metric("epever_battery_temperature_celsius", battery_temperature);
  if (read_register(ctx, 0x311A, &battery_soc)) {
      bye(ctx, "Reading battery SOC failed");
  }
  metric("epever_battery_soc", battery_soc);
  //printf("battery: %.2f V | %.2f C\n", battery_voltage, battery_temperature);

  //float battery_min, battery_max;
  //read_register(ctx, 0x3303, &battery_min);
  //read_register(ctx, 0x3302, &battery_max);
  //printf("battery today: %.2f V min | %.2f V max\n", battery_min, battery_max);

  //printf("READING REGISTERS...\n");
  //rc = modbus_read_input_registers(ctx, 0x3001, 1, tab_reg);
  //if (rc == -1) {
  //  fprintf(stderr, "%s\n", modbus_strerror(errno));
  //  modbus_free(ctx);
  //  return -1;
  //}

  //for (int i = 0; i < rc; i++) {
  //  printf("reg[%d]=%d (0x%X)\n", i, tab_reg[i], tab_reg[i]);
  //}

  modbus_close(ctx);
  modbus_free(ctx);
}
