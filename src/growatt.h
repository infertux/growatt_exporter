#include <inttypes.h>

struct growatt_register {
    uint8_t address;
    char human_name[32];
    char metric_name[32];
    enum RegisterSize { REGISTER_SINGLE, REGISTER_DOUBLE } register_size;
    double scale;
};

//# REGISTER_BATTERY_VOLTAGE 17
//# REGISTER_BATTERY_SOC 18
//# REGISTER_TEMPERATURE_INVERTER 25
//# REGISTER_TEMPERATURE_DCDC 26
//# REGISTER_TEMPERATURE_BUCK1 32
//# REGISTER_TEMPERATURE_BUCK2 33
//# REGISTER_ENERGY_PV_TODAY 48
//# REGISTER_ENERGY_PV_TOTAL 50
//# REGISTER_FAN_SPEED_MPPT 81
//# REGISTER_FAN_SPEED_INVERTER 82

struct growatt_register growatt_registers[3] = {
    { 0, "system status", "system_status", REGISTER_SINGLE, 1 },
    { 1, "PV voltage", "pv_volts", REGISTER_SINGLE, 0.1 },
    { 3, "PV power", "pv_watts", REGISTER_DOUBLE, 0.1 }
};

/*
  double battery_voltage = 0;
  if (-1 == read_input_register_scaled_by(ctx, REGISTER_BATTERY_VOLTAGE,
                                          &battery_voltage, 1.0 / 100.0)) {
    read_register_failed(id, "battery voltage");
  } else {
    add_metric("battery_volts", battery_voltage);
  }

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

  double temperature_buck1 = 0;
  if (-1 == read_input_register_scaled(ctx, REGISTER_TEMPERATURE_BUCK1,
                                       &temperature_buck1)) {
    read_register_failed(id, "Buck1 temperature");
  } else {
    add_metric("temperature_buck1_celsius", temperature_buck1);
  }

  double temperature_buck2 = 0;
  if (-1 == read_input_register_scaled(ctx, REGISTER_TEMPERATURE_BUCK2,
                                       &temperature_buck2)) {
    read_register_failed(id, "Buck2 temperature");
  } else {
    add_metric("temperature_buck2_celsius", temperature_buck2);
  }

  double energy_pv_today = 0;
  if (-1 == read_input_register_double_scaled(ctx, REGISTER_ENERGY_PV_TODAY,
                                       &energy_pv_today)) {
    read_register_failed(id, "PV energy today");
  } else {
    add_metric("energy_pv_today_kwh", energy_pv_today);
  }

  double energy_pv_total = 0;
  if (-1 == read_input_register_double_scaled(ctx, REGISTER_ENERGY_PV_TOTAL,
                                       &energy_pv_total)) {
    read_register_failed(id, "PV energy total");
  } else {
    add_metric("energy_pv_total_kwh", energy_pv_total);
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
*/
