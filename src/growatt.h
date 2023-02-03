#include <inttypes.h>

enum {
  MAX_METRIC_LENGTH = 64,
  CLOCK_OFFSET_THRESHOLD = 30, // seconds
  REGISTER_CLOCK_ADDRESS = 45,
  REGISTER_CLOCK_SIZE = 6,
  REGISTER_CLOCK_YEAR_OFFSET = -1900, // years
};

typedef struct __attribute__((aligned(MAX_METRIC_LENGTH * 2))) {
  uint8_t address;
  char human_name[MAX_METRIC_LENGTH];
  char metric_name[MAX_METRIC_LENGTH];
  enum { REGISTER_SINGLE, REGISTER_DOUBLE } register_size;
  double scale;
} REGISTER;

const REGISTER holding_registers[] = {
    // NOLINTBEGIN(cppcoreguidelines-avoid-magic-numbers,readability-magic-numbers)
    {34, "max charging current", "settings_max_charging_amps", REGISTER_SINGLE, 1},
    {35, "bulk charging voltage", "settings_bulk_charging_volts", REGISTER_SINGLE, 0.1},
    {36, "float charging voltage", "settings_float_charging_volts", REGISTER_SINGLE, 0.1},
    {37, "battery voltage switch to utility", "settings_switch_to_utility_volts", REGISTER_SINGLE,
     0.1},
    // XXX: not needed
    // {76, "rated active power", "rated_active_power_watts", REGISTER_DOUBLE, 0.1},
    // XXX: not needed
    // {78, "rated apparant power", "rated_apparant_power_va", REGISTER_DOUBLE, 0.1},
    // NOLINTEND(cppcoreguidelines-avoid-magic-numbers,readability-magic-numbers)
};

const REGISTER input_registers[] = {
    // NOLINTBEGIN(cppcoreguidelines-avoid-magic-numbers,readability-magic-numbers)
    {0, "system status", "system_status", REGISTER_SINGLE, 1},
    {1, "PV1 voltage", "pv1_volts", REGISTER_SINGLE, 0.1},
    {3, "PV1 power", "pv1_watts", REGISTER_DOUBLE, 0.1},
    {7, "buck1 current", "buck1_amps", REGISTER_SINGLE, 0.1},
    // {8, "buck2 current", "buck2_amps", REGISTER_SINGLE, 0.1}, // XXX: always zero
    {9, "inverter active power", "inverter_active_power_watts", REGISTER_DOUBLE, 0.1},
    {11, "inverter apparant power", "inverter_apparant_power_va", REGISTER_DOUBLE, 0.1},
    {13, "grid charging power", "grid_charging_watts", REGISTER_DOUBLE, 0.1},
    {17, "battery voltage", "battery_volts", REGISTER_SINGLE, 0.01},
    {18, "battery SOC", "battery_soc", REGISTER_SINGLE, 1},
    // {19, "bus voltage", "bus_volts", REGISTER_SINGLE, 0.1}, // irrelevant
    {20, "grid voltage", "grid_volts", REGISTER_SINGLE, 0.1},
    {21, "grid frequency", "grid_hz", REGISTER_SINGLE, 0.01},
    // {24, "output DC voltage", "output_dc_volts", REGISTER_SINGLE, 0.1}, // XXX: always zero
    {25, "inverter temperature", "temperature_inverter_celsius", REGISTER_SINGLE, 0.1},
    {26, "DC-DC temperature", "temperature_dcdc_celsius", REGISTER_SINGLE, 0.1},
    {27, "inverter load percent", "inverter_load_percent", REGISTER_SINGLE, 0.1},
    // {30, "work time total", "work_time_total_seconds", REGISTER_DOUBLE, 0.5}, // XXX: always zero
    {32, "buck1 temperature", "temperature_buck1_celsius", REGISTER_SINGLE, 0.1},
    // {33, "buck2 temperature", "temperature_buck2_celsius", REGISTER_SINGLE, 0.1}, // irrelevant
    {34, "output current", "output_amps", REGISTER_SINGLE, 0.1},
    {35, "inverter current", "inverter_amps", REGISTER_SINGLE, 0.1},
    {40, "fault bit", "fault_bit", REGISTER_SINGLE, 1},
    {41, "warning bit", "warning_bit", REGISTER_SINGLE, 1},
    // {42, "fault value", "fault_value", REGISTER_SINGLE, 1}, // XXX: always zero
    // {43, "warning value", "warning_value", REGISTER_SINGLE, 1}, // XXX: always zero
    {48, "PV energy today", "energy_pv_today_kwh", REGISTER_DOUBLE, 0.1},
    {50, "PV energy total", "energy_pv_total_kwh", REGISTER_DOUBLE, 0.1},
    {56, "grid energy today", "energy_grid_today_kwh", REGISTER_DOUBLE, 0.1},
    {58, "grid energy total", "energy_grid_total_kwh", REGISTER_DOUBLE, 0.1},
    {60, "battery discharging energy today", "battery_discharging_today_kwh", REGISTER_DOUBLE, 0.1},
    {64, "grid discharging energy today", "grid_discharging_today_kwh", REGISTER_DOUBLE,
     0.1}, // FIXME: irrelevant?
    {66, "grid discharging energy total", "grid_discharging_total_kwh", REGISTER_DOUBLE,
     0.1}, // FIXME: irrelevant?
    {68, "grid charging current", "grid_charging_amps", REGISTER_SINGLE, 0.1},
    {69, "inverter discharging power", "inverter_discharging_watts", REGISTER_DOUBLE, 0.1},
    {73, "battery discharging power", "battery_discharging_watts", REGISTER_DOUBLE, 0.1},
    {77, "battery net power (signed)", "battery_net_watts", REGISTER_DOUBLE, -0.1},
    // {81, "fan speed MPPT", "fan_speed_mppt", REGISTER_SINGLE, 1}, // XXX: always zero
    {82, "fan speed inverter", "fan_speed_inverter", REGISTER_SINGLE, 1},
    // {180, "solar charger status", "solar_status", REGISTER_SINGLE, 1}, // XXX: always zero
    // NOLINTEND(cppcoreguidelines-avoid-magic-numbers,readability-magic-numbers)
};
