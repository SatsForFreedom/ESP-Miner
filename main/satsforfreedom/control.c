#include "control.h"
#include <math.h>

/************************************************************************************************************
 * @brief Convert a board 2.A core-voltage request to a DS4432U DAC code.
 * @param[in] volts Requested voltage in volts; must be finite and in [1, 1.5].
 * @param[out] code Non-NULL destination for the direction/magnitude byte.
 *                  Left unchanged if conversion fails.
 * @return true when a code is produced; false for invalid input or magnitude.
 * @note Uses the board's 33 kOhm/22 kOhm feedback and 100 kOhm full-scale
 *       resistors, with the legacy one-step correction clamped at zero.
 *       Pure calculation: does not access I2C or change regulator voltage.
 ***********************************************************************************************************/
bool sff_voltage_code(float volts, uint8_t *code)
{
    if (!code || !isfinite(volts) || volts < 1.0f || volts > 1.5f) return false;
    const float full_scale = 0.997f * 127.0f / (100000.0f * 16.0f);
    float change = fabsf((0.6f / 22000.0f - (volts - 0.6f) / 33000.0f) / full_scale * 127.0f);
    /* Preserve the fork's calibration without its underflow at nominal voltage. */
    int magnitude = (int)lroundf(change) - 1;
    if (magnitude < 0) magnitude = 0;
    if (magnitude > 127) return false;
    *code = (uint8_t)magnitude | (volts < 1.5f && magnitude ? 0x80 : 0);
    return true;
}

/************************************************************************************************************
 * @brief Calculate one bounded power-control frequency adjustment.
 * @param[in] current Current requested frequency in MHz; invalid or sub-50
 *                    values start from 50 MHz.
 * @param[in] maximum Frequency ceiling in MHz, capped at 580 MHz.
 * @param[in] watts Measured input power in watts; must be finite and positive.
 * @param[in] limit Power target in watts; must be finite and at least 1 W.
 * @param[in] temp Chip temperature in degrees Celsius; finite and nonnegative.
 * @return Next target between 50 MHz and the capped valid ceiling, or 50 MHz
 *         for an invalid ceiling or measurement/limit input.
 * @note Stateless calculation for an approximately 10 Hz loop. Reduces the
 *       power target from 65 to 75 C. Each adjustment is limited to -20/+2 MHz
 *       before applying frequency bounds; a lowered ceiling takes precedence.
 *       Does not drive hardware or guarantee an instantaneous power limit.
 ***********************************************************************************************************/
float sff_power_frequency(float current, float maximum, float watts, float limit, float temp)
{
    if (!isfinite(maximum) || maximum < 50.0f) return 50.0f;
    if (maximum > 580.0f) maximum = 580.0f;
    if (!isfinite(current) || current < 50.0f) current = 50.0f;
    if (!isfinite(watts) || watts <= 0 || !isfinite(temp) || temp < 0) return 50.0f;
    if (!isfinite(limit) || limit < 1.0f) return 50.0f;
    /* Run at 10 Hz. Upstream thermal shutdown still takes precedence. */
    float thermal = fmaxf(0.0f, fminf(1.0f, (75.0f - temp) / 10.0f));
    float delta = (limit * thermal - watts) * 0.008f * maximum;
    delta = fmaxf(-20.0f, fminf(2.0f, delta));
    return fmaxf(50.0f, fminf(maximum, current + delta));
}
