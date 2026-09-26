#ifndef SFF_CONTROL_H
#define SFF_CONTROL_H
#include <stdbool.h>
#include <stdint.h>
/* Pure control/calibration functions, also compiled by the host tests. */
bool sff_voltage_code(float volts, uint8_t *code);
float sff_power_frequency(float current_mhz, float maximum_mhz, float watts,
                          float limit_watts, float temperature_c);
#endif
