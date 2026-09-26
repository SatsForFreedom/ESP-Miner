#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "crc.h"
#define TYPE_CMD 0x40
#define GROUP_SINGLE 0
#define GROUP_ALL 0x10
#define CMD_READ 2
#define CMD_WRITE 1
#define MISC_CONTROL 0x18
#define BM1397_CHIP_ID 0x1397
#define BM1397_CHIP_ID_RESPONSE_LENGTH 9
#define ESP_OK 0
#define pdMS_TO_TICKS(x) (x)
static uint8_t reply[9];
static int reply_length = 9;
static bool detected;
static int selected_baud;
static int transmissions;
static void SERIAL_clear_buffer(void) {}
static void vTaskDelay(int ticks) { (void)ticks; }
static int SERIAL_set_baud(int baud) { selected_baud = baud; return ESP_OK; }
static int SERIAL_rx(uint8_t *dest, uint16_t size, uint16_t timeout)
{
    (void)timeout;
    memcpy(dest, reply, size);
    return reply_length;
}
static void _send_BM1397(uint8_t header, uint8_t *data, uint8_t size, bool debug)
{
    (void)header; (void)data; (void)size; (void)debug;
    ++transmissions;
}
static void _send_read_address(void) {}
static int count_asic_chips(uint16_t count, uint16_t id, int length)
{
    assert(count == 1 && id == 0x1397 && length == 9);
    return detected ? 1 : 0;
}
#include "../../components/asic/bm1397_satsforfreedom.inc"
int main(void)
{
    uint8_t setting[] = {0, 0x80, 0, 0, 0, 1};
    uint8_t good[] = {0xAA, 0x55, 0, 0, 0, 1, 0, 0x80, 0};
    while (good[8] < 32 && crc5(good + 2, 7) != 0) ++good[8];
    assert(good[8] < 32);
    memcpy(reply, good, sizeof(reply));
    assert(sff_bm1397_verify(setting));
    reply_length = 8;
    assert(!sff_bm1397_verify(setting));
    reply_length = 9;
    for (int i = 0; i < 9; ++i) {
        memcpy(reply, good, sizeof(reply));
        reply[i] ^= 1;
        assert(!sff_bm1397_verify(setting));
    }
    detected = false;
    transmissions = 0;
    assert(sff_bm1397_baud() == 0);
    assert(selected_baud == 3125000 && transmissions == 1);
    /* Caller resets the chip before this next attempt. */
    detected = true;
    assert(sff_bm1397_baud() == 1562500);
    assert(sff_bm1397_baud() == 3125000);
    puts("SatsForFreedom UART validation and fallback tests passed");
}
