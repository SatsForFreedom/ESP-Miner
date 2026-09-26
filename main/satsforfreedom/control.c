#include "control.h"
#include <math.h>

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
