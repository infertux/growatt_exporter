#ifndef GROWATT_GROWATT_H
#define GROWATT_GROWATT_H

#include <inttypes.h>
#include <signal.h>

#define COUNT(x) (sizeof(x) / sizeof((x)[0])) // NOLINT(bugprone-sizeof-expression)

// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
static volatile sig_atomic_t keep_running = 1;

enum {
  CLOCK_OFFSET_THRESHOLD = 30, // seconds
  EXIT_NO_METRICS = 4,
  MAX_METRIC_LENGTH = 64U,
  REGISTER_CLOCK_ADDRESS = 45,
  REGISTER_CLOCK_SIZE = 6U,
  REGISTER_CLOCK_YEAR_OFFSET = -1900, // years
};

typedef struct __attribute__((aligned(MAX_METRIC_LENGTH * 2))) {
  uint8_t address;
  char human_name[MAX_METRIC_LENGTH];
  char metric_name[MAX_METRIC_LENGTH];
  char device_class[MAX_METRIC_LENGTH];
  char unit[MAX_METRIC_LENGTH];
  enum { REGISTER_SINGLE, REGISTER_DOUBLE } register_size;
  double scale;
} REGISTER;

const REGISTER holding_registers[] = {
    // NOLINTBEGIN(readability-magic-numbers)
    {34, "max charging current", "settings_max_charging_amps", "current", "A", REGISTER_SINGLE, 1},
    {35, "bulk charging voltage", "settings_bulk_charging_volts", "voltage", "V", REGISTER_SINGLE, 0.1},
    {36, "float charging voltage", "settings_float_charging_volts", "voltage", "V", REGISTER_SINGLE, 0.1},
    {37, "battery voltage switch to utility", "settings_switch_to_utility_volts", "voltage", "V", REGISTER_SINGLE, 0.1},
    // {76, "rated active power", "rated_active_power_watts", REGISTER_DOUBLE, 0.1}, // XXX: not needed
    // {78, "rated apparant power", "rated_apparant_power_va", REGISTER_DOUBLE, 0.1}, // XXX: not needed
    // NOLINTEND(readability-magic-numbers)
};

const REGISTER input_registers[] = {
    // NOLINTBEGIN(readability-magic-numbers)
    {0, "system status", "system_status", "None", "", REGISTER_SINGLE, 1},
    {1, "PV1 voltage", "pv1_volts", "voltage", "V", REGISTER_SINGLE, 0.1},
    {3, "PV1 power", "pv1_watts", "power", "W", REGISTER_DOUBLE, 0.1},
    {7, "buck1 current", "buck1_amps", "current", "A", REGISTER_SINGLE, 0.1},
    // {8, "buck2 current", "buck2_amps", "current", "A", REGISTER_SINGLE, 0.1}, // XXX: always zero
    {9, "inverter active power", "inverter_active_power_watts", "power", "W", REGISTER_DOUBLE, 0.1},
    {11, "inverter apparant power", "inverter_apparant_power_va", "apparent_power", "VA", REGISTER_DOUBLE, 0.1},
    {13, "grid charging power", "grid_charging_watts", "power", "W", REGISTER_DOUBLE, 0.1},
    {17, "battery voltage", "battery_volts", "voltage", "V", REGISTER_SINGLE, 0.01},
    {18, "battery SOC", "battery_soc", "battery", "%", REGISTER_SINGLE, 1},
    // {19, "bus voltage", "bus_volts", REGISTER_SINGLE, 0.1}, // irrelevant
    {20, "grid voltage", "grid_volts", "voltage", "V", REGISTER_SINGLE, 0.1},
    {21, "grid frequency", "grid_hz", "frequency", "Hz", REGISTER_SINGLE, 0.01},
    // {24, "output DC voltage", "output_dc_volts", REGISTER_SINGLE, 0.1}, // XXX: always zero
    {25, "inverter temperature", "temperature_inverter_celsius", "temperature", "°C", REGISTER_SINGLE, 0.1},
    {26, "DC-DC temperature", "temperature_dcdc_celsius", "temperature", "°C", REGISTER_SINGLE, 0.1},
    {27, "inverter load percent", "inverter_load_percent", "None", "%", REGISTER_SINGLE, 0.1},
    // {30, "work time total", "work_time_total_seconds", REGISTER_DOUBLE, 0.5}, // XXX: always zero
    {32, "buck1 temperature", "temperature_buck1_celsius", "temperature", "°C", REGISTER_SINGLE, 0.1},
    // {33, "buck2 temperature", "temperature_buck2_celsius", REGISTER_SINGLE, 0.1}, // irrelevant
    {34, "output current", "output_amps", "current", "A", REGISTER_SINGLE, 0.1},
    {35, "inverter current", "inverter_amps", "current", "A", REGISTER_SINGLE, 0.1},
    {40, "fault bit", "fault_bit", "None", "", REGISTER_SINGLE, 1},
    {41, "warning bit", "warning_bit", "None", "", REGISTER_SINGLE, 1},
    // {42, "fault value", "fault_value", REGISTER_SINGLE, 1}, // XXX: always zero
    // {43, "warning value", "warning_value", REGISTER_SINGLE, 1}, // XXX: always zero
    // {45, "product check step", "product_check_step", REGISTER_SINGLE, 1}, // irrelevant
    // {46, "production line mode", "production_line_mode", REGISTER_SINGLE, 1}, // XXX: always zero
    // {47, "constant power OK flag", "constant_power_ok_flag", REGISTER_SINGLE, 1}, // XXX: always zero
    {48, "PV energy today", "energy_pv_today_kwh", "energy", "kWh", REGISTER_DOUBLE, 0.1},
    {50, "PV energy total", "energy_pv_total_kwh", "energy", "kWh", REGISTER_DOUBLE, 0.1},
    {56, "grid energy today", "energy_grid_today_kwh", "energy", "kWh", REGISTER_DOUBLE, 0.1},
    {58, "grid energy total", "energy_grid_total_kwh", "energy", "kWh", REGISTER_DOUBLE, 0.1},
    {60, "battery discharging energy today", "battery_discharging_today_kwh", "energy", "kWh", REGISTER_DOUBLE, 0.1},
    {64, "grid discharging energy today", "grid_discharging_today_kwh", "energy", "kWh", REGISTER_DOUBLE, 0.1},
    {66, "grid discharging energy total", "grid_discharging_total_kwh", "energy", "kWh", REGISTER_DOUBLE, 0.1},
    {68, "grid charging current", "grid_charging_amps", "current", "A", REGISTER_SINGLE, 0.1},
    {69, "inverter discharging power", "inverter_discharging_watts", "power", "W", REGISTER_DOUBLE, 0.1},
    {73, "battery discharging power", "battery_discharging_watts", "power", "W", REGISTER_DOUBLE, 0.1},
    {77, "battery net power", "battery_net_watts", "power", "W", REGISTER_DOUBLE, -0.1}, // XXX: signed value
    // {81, "fan speed MPPT", "fan_speed_mppt", REGISTER_SINGLE, 1}, // XXX: always zero
    {82, "fan speed inverter", "fan_speed_inverter", "wind_speed", "%", REGISTER_SINGLE, 1},
    // {180, "solar charger status", "solar_status", REGISTER_SINGLE, 1}, // XXX: always zero
    // NOLINTEND(readability-magic-numbers)
};

#endif /* GROWATT_GROWATT_H */
