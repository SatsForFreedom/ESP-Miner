/************************************************************************************************************
 * @brief Read back a BM1397 initialization register and verify its value.
 * @param[in] setting Non-NULL six-byte write payload: address, register, and
 *                    four expected value bytes. The read targets chip zero.
 * @return true for a complete command reply with valid preamble, register,
 *         CRC, and matching value; false for a timeout or validation failure.
 * @note Clears pending UART input, sends a register-read command, and waits
 *       up to the 100 ms receive timeout. Requires exclusive UART access
 *       during initialization, before mining tasks consume replies.
 ***********************************************************************************************************/
static bool sff_bm1397_verify(const uint8_t *setting)
{
    uint8_t request[] = {0x00, setting[1]};
    uint8_t reply[9] = {0};
    SERIAL_clear_buffer();
    _send_BM1397(TYPE_CMD | GROUP_SINGLE | CMD_READ, request, sizeof(request), false);
    int received = SERIAL_rx(reply, sizeof(reply), 100);
    return received == (int)sizeof(reply) && reply[0] == 0xAA && reply[1] == 0x55 &&
           !(reply[8] & 0x80) && reply[7] == setting[1] &&
           crc5(reply + 2, sizeof(reply) - 2) == 0 && memcmp(reply + 2, setting + 2, 4) == 0;
}

/************************************************************************************************************
 * @brief Probe one candidate UART baud rate for a single BM1397.
 * @return Verified baud rate in bits per second, or zero if the probe fails.
 * @note Requires an initialized chip and exclusive UART access. Changes both
 *       ASIC and host baud rates, waits 100 ms, clears UART input, and probes
 *       chip identity. A private divider advances after failure (27 candidates)
 *       and resets after success. The caller must reset and reinitialize the
 *       ASIC after failure before calling again; this function does not restore
 *       the previous baud rate. Chip detection adds its own receive wait.
 ***********************************************************************************************************/
static int sff_bm1397_baud(void)
{
    /* Each failed attempt is followed by hardware reset/reinitialization.
       Never send a fallback command over a UART link that failed verification. */
    static uint8_t divider;
    uint8_t command[] = {0x00, MISC_CONTROL, 0x00, 0x00, (uint8_t)(0x60 | divider), 0x31};
    int baud = 3125000 / (divider + 1);
    _send_BM1397(TYPE_CMD | GROUP_ALL | CMD_WRITE, command, sizeof(command), false);
    if (SERIAL_set_baud(baud) == ESP_OK) {
        vTaskDelay(pdMS_TO_TICKS(100));
        SERIAL_clear_buffer();
        _send_read_address();
        if (count_asic_chips(1, BM1397_CHIP_ID, BM1397_CHIP_ID_RESPONSE_LENGTH) == 1) {
            divider = 0;
            return baud;
        }
    }
    divider = (divider + 1) % 27;
    return 0;
}
