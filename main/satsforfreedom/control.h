#ifndef SFF_CONTROL_H
#define SFF_CONTROL_H
#include <stdbool.h>
#include <stdint.h>
/************************************************************************************************************
 * @brief Convert a board 2.A voltage request to a DS4432U DAC code.
 * @param[in] volts Requested core voltage in volts, finite and within [1.0, 1.5].
 * @param[out] code Non-NULL destination for the DAC direction/magnitude byte.
 * @return true on conversion; false for invalid inputs or an unrepresentable code.
 * @note Pure calculation using the board resistor values; does not access I2C.
 ***********************************************************************************************************/
bool sff_voltage_code(float volts, uint8_t *code);

/************************************************************************************************************
 * @brief Calculate one bounded adjustment to the ASIC frequency target.
 * @param[in] current Current frequency target in MHz.
 * @param[in] maximum Configured upper frequency bound in MHz.
 * @param[in] watts Measured power in watts; must be finite and positive.
 * @param[in] limit_watts Power target in watts; must be finite and at least 1.
 * @param[in] temperature_c Chip temperature in Celsius; must be finite/nonnegative.
 * @return Next target in MHz, bounded to 50 MHz and the capped upper frequency.
 * @note Stateless control step. Limits adjustment to -20/+2 MHz and reduces
 *       the power target between 65 and 75 C. Does not control hardware directly.
 ***********************************************************************************************************/
float sff_power_frequency(float current_mhz, float maximum_mhz, float watts,
                          float limit_watts, float temperature_c);
#endif
