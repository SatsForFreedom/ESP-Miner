#include <assert.h>
#include <math.h>
#include <stdio.h>
#include "control.h"
int main(void)
{
    uint8_t code;
    assert(!sff_voltage_code(NAN, &code));
    assert(!sff_voltage_code(INFINITY, &code));
    assert(!sff_voltage_code(1.501f, &code));
    assert(!sff_voltage_code(0.99f, &code));
    assert(sff_voltage_code(1.5f, &code) && code == 0);
    assert(sff_voltage_code(1.4f, &code) && code == 0x84);
    for (int mv = 1000; mv <= 1500; ++mv) {
        assert(sff_voltage_code(mv / 1000.0f, &code));
        assert((code & 127) <= 31);
    }
    assert(sff_power_frequency(300, 500, 15, 12, 50) < 300);
    assert(sff_power_frequency(300, 500, 8, 12, 50) > 300);
    assert(sff_power_frequency(300, 500, 8, 12, 75) < 300);
    assert(sff_power_frequency(300, 500, NAN, 12, 50) == 50);
    assert(sff_power_frequency(300, 500, 0, 12, 50) == 50);
    assert(sff_power_frequency(300, 500, 8, 12, -1) == 50);
    assert(sff_power_frequency(500, 200, 8, 12, 50) <= 200);
    float frequency = 50;
    for (int i = 0; i < 10000; ++i) {
        frequency = sff_power_frequency(frequency, 500, 2 + frequency * 0.04f, 12, 50);
        assert(frequency >= 50 && frequency <= 500);
    }
    assert(fabsf(frequency - 250) < 1);
    puts("SatsForFreedom calibration and power control tests passed");
}
