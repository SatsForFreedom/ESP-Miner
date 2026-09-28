#include "satsforfreedom.h"
#include "cJSON.h"
#include "control.h"
#include "global_state.h"
#include "nvs_config.h"
#include "vcore.h"
#include "adc.h"
#include "esp_timer.h"
#include "esp_system.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "driver/uart.h"
#include "freertos/task.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <inttypes.h>
#include <math.h>

static bool board_2a;
static float mean_efficiency;
static unsigned efficiency_samples;
static float filtered_power;
static bool power_sampled;
static int64_t last_result_us;
static int64_t last_work_us;
static int64_t last_job_response_us;
static int64_t zero_hash_since_us;
static int64_t last_health_log_us;
static uint32_t work_packets_sent;
static uint32_t job_responses_received;
static uint32_t register_responses_received;
static uint32_t health_work_packets_sent;
static uint32_t health_job_responses_received;
static uint32_t health_register_responses_received;
static char session_id[129];
static portMUX_TYPE info_lock = portMUX_INITIALIZER_UNLOCKED;

/************************************************************************************************************
 * @brief Report whether the configured device is board 2.A.
 * @return true for board 2.A; false for other boards or before configuration.
 * @note Reads the board selection cached by sff_configure(); has no side effects.
 ***********************************************************************************************************/
bool sff_board(void) { return board_2a; }
/************************************************************************************************************
 * @brief Select the GPIO used to enable the ASIC core regulator.
 * @param[in] default_pin GPIO number to use for boards other than 2.A.
 * @return GPIO14 for board 2.A, otherwise default_pin.
 * @note Uses the cached board selection; does not configure or drive the GPIO.
 ***********************************************************************************************************/
int sff_enable_pin(int default_pin) { return board_2a ? 14 : default_pin; }

/************************************************************************************************************
 * @brief Apply the board 2.A hardware profile during device initialization.
 * @param[in,out] g Non-NULL device state with board_version already populated.
 * @return None.
 * @note Caches the board selection. For board 2.A, updates the device profile
 *       and configures GPIO10 as an output driven low for the ASIC BI signal.
 *       Call once at startup, before ADC initialization and worker tasks.
 ***********************************************************************************************************/
void sff_configure(GlobalState *g)
{
    board_2a = g->DEVICE_CONFIG.board_version && strcmp(g->DEVICE_CONFIG.board_version, "2.A") == 0;
    if (!board_2a) return;
    g->DEVICE_CONFIG.family = FAMILY_MAX;
    g->DEVICE_CONFIG.family.asic.difficulty = 128;
    g->DEVICE_CONFIG.family.asic.init_retry_attempts = 27;
    g->DEVICE_CONFIG.pins = (DevicePins)BITAXE_ORIGINAL_PINS;
    g->DEVICE_CONFIG.EMC2101 = true;
    g->DEVICE_CONFIG.emc_internal_temp = false;
    g->DEVICE_CONFIG.DS4432U = true;
    g->DEVICE_CONFIG.INA260 = true;
    g->DEVICE_CONFIG.asic_enable = true;
    g->DEVICE_CONFIG.asic_enable_active_high = false;
    g->DEVICE_CONFIG.plug_sense = false;
    g->DEVICE_CONFIG.power_consumption_target = 12;
    /* Board 2.A BI signal; independent of regulator enable on GPIO14. */
    gpio_set_direction(GPIO_NUM_10, GPIO_MODE_OUTPUT);
    gpio_set_level(GPIO_NUM_10, 0);
}

/************************************************************************************************************
 * @brief Read the TPS40305 PGOOD signal on board 2.A.
 * @return ESP_OK when PGOOD is high or the selected board is not 2.A; ESP_FAIL
 *         when board 2.A reports PGOOD low.
 * @note Configures GPIO11 as an input and logs the result. The caller may log
 *       a failure without aborting startup, as the original firmware did.
 ***********************************************************************************************************/
esp_err_t sff_check_pgood(void)
{
    if (!board_2a) return ESP_OK;

    gpio_set_direction(GPIO_NUM_11, GPIO_MODE_INPUT);
    if (gpio_get_level(GPIO_NUM_11) == 1) {
        ESP_LOGI("satsforfreedom", "TPS40305 reports power good.");
        return ESP_OK;
    }

    ESP_LOGE("satsforfreedom", "TPS40305 PGOOD is low.");
    return ESP_FAIL;
}

/************************************************************************************************************
 * @brief Check board 2.A core voltage before initializing the ASIC.
 * @param[in,out] g Non-NULL device state; supplies the self-test voltage target
 *                  when active, otherwise the configured NVS target is used.
 * @return ESP_OK for other boards, or when the target is 1000-1500 mV and the
 *         ten-sample ADC average is within 150 mV; ESP_FAIL otherwise.
 * @note Requires initialized ADC, NVS, and regulator interfaces. Blocks for
 *       approximately one second on board 2.A. On failure, requests regulator
 *       shutdown and records a hardware fault and ASIC status in g. The
 *       shutdown request's return value is not checked here.
 ***********************************************************************************************************/
esp_err_t sff_check_voltage(GlobalState *g)
{
    if (!board_2a) return ESP_OK;
    float target = g->SELF_TEST_MODULE.is_active ? g->DEVICE_CONFIG.family.asic.default_voltage_mv
                                               : nvs_config_get_u16(NVS_CONFIG_ASIC_VOLTAGE);
    unsigned total = 0;
    for (int i = 0; i < 10; ++i) {
        total += ADC_get_vcore();
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    if (target >= 1000 && target <= 1500 && fabsf(total / 10.0f - target) <= 150.0f) return ESP_OK;
    VCORE_set_voltage(g, 0);
    g->SYSTEM_MODULE.hardware_fault = true;
    snprintf(g->SYSTEM_MODULE.hardware_fault_msg, sizeof(g->SYSTEM_MODULE.hardware_fault_msg),
             "Core voltage outside 150 mV tolerance");
    g->SYSTEM_MODULE.asic_status = "Core voltage check failed";
    return ESP_FAIL;
}

/************************************************************************************************************
 * @brief Calculate the next BM1397 frequency target under power control.
 * @param[in] g Non-NULL device state containing current sensor readings,
 *              hashrate, frequency, ASIC initialization, and self-test state.
 * @param[in] maximum Configured frequency ceiling in MHz.
 * @return maximum when control is bypassed; otherwise a target in 50-580 MHz,
 *         bounded by a valid ceiling. Returns 50 MHz while uninitialized or
 *         when required measurements are invalid.
 * @note Reads the NVS power limit in mW; zero means 12 W on board 2.A and
 *       disables control on other boards. Updates shared efficiency statistics
 *       and a private power filter. Call from the single power-management task
 *       at roughly 10 Hz. Does not apply frequency or replace thermal shutdown.
 ***********************************************************************************************************/
float sff_frequency(GlobalState *g, float maximum)
{
    if (g->DEVICE_CONFIG.family.asic.id != BM1397 || g->SELF_TEST_MODULE.is_active) return maximum;
    uint16_t limit = nvs_config_get_u16(NVS_CONFIG_SFF_POWER_LIMIT);
    if (!limit && !board_2a) return maximum;
    if (!limit) limit = 12000;
    if (!g->ASIC_initalized) return 50.0f;
    PowerManagementModule *p = &g->POWER_MANAGEMENT_MODULE;
    float rate = g->SYSTEM_MODULE.current_hashrate;
    if (isfinite(rate) && rate > 0.1f && isfinite(p->power) && p->power > 0) {
        portENTER_CRITICAL(&info_lock);
        mean_efficiency += (p->power * 1000.0f / rate - mean_efficiency) / (efficiency_samples + 1);
        if (efficiency_samples < 100) ++efficiency_samples;
        portEXIT_CRITICAL(&info_lock);
    }
    if (isfinite(p->power) && p->power > 0) {
        filtered_power = power_sampled ? filtered_power * 0.95f + p->power * 0.05f : p->power;
        power_sampled = true;
    } else {
        power_sampled = false;
        return 50.0f;
    }
    /* React immediately above the target; smooth only recovery/increases. */
    float measured = fmaxf(filtered_power, p->power);
    return sff_power_frequency(p->frequency_value, maximum, measured, limit / 1000.0f, p->chip_temp_avg);
}

/************************************************************************************************************
 * @brief Store the current Stratum V1 session identifier for telemetry.
 * @param[in] session NUL-terminated identifier to copy, or NULL to clear it.
 * @return None.
 * @note Copies at most 128 characters into shared storage under info_lock;
 *       the caller retains ownership of the input string.
 ***********************************************************************************************************/
void sff_session(const char *session)
{
    portENTER_CRITICAL(&info_lock);
    snprintf(session_id, sizeof(session_id), "%s", session ? session : "");
    portEXIT_CRITICAL(&info_lock);
}

/************************************************************************************************************
 * @brief Record a work packet sent to the board 2.A BM1397.
 * @return None.
 * @note Increments the diagnostic work counter and records the latest transmit
 *       time under info_lock. Other boards are ignored. This function does not
 *       alter work submission or ASIC state.
 ***********************************************************************************************************/
void sff_work_sent(void)
{
    if (!board_2a) return;
    int64_t now = esp_timer_get_time();
    portENTER_CRITICAL(&info_lock);
    ++work_packets_sent;
    last_work_us = now;
    portEXIT_CRITICAL(&info_lock);
}

/************************************************************************************************************
 * @brief Record a valid response received from the board 2.A BM1397.
 * @param[in] job_response true for a nonce/job result; false for a register reply.
 * @return None.
 * @note Updates the corresponding diagnostic counter under info_lock and, for
 *       job results, records the latest response time. Other boards are ignored.
 ***********************************************************************************************************/
void sff_response(bool job_response)
{
    if (!board_2a) return;
    int64_t now = esp_timer_get_time();
    portENTER_CRITICAL(&info_lock);
    if (job_response) {
        ++job_responses_received;
        last_job_response_us = now;
    } else {
        ++register_responses_received;
    }
    portEXIT_CRITICAL(&info_lock);
}

/************************************************************************************************************
 * @brief Append SFF power-limit, efficiency, and session telemetry to JSON.
 * @param[in,out] root Non-NULL JSON object receiving powerLimitMilliwatts
 *                     (mW), meanEfficiency (W/TH/s), and sessionId (string).
 * @param[in] g Reserved device-state parameter; currently unused.
 * @return None.
 * @note Reads NVS and snapshots shared telemetry under info_lock. cJSON
 *       allocates the new fields, which root owns. Allocation failures are
 *       not reported; call once per object to avoid duplicate field names.
 ***********************************************************************************************************/
void sff_add_info(cJSON *root, GlobalState *g)
{
    char session[sizeof(session_id)];
    float efficiency;
    portENTER_CRITICAL(&info_lock);
    memcpy(session, session_id, sizeof(session));
    efficiency = mean_efficiency;
    portEXIT_CRITICAL(&info_lock);
    cJSON_AddNumberToObject(root, "powerLimitMilliwatts", nvs_config_get_u16(NVS_CONFIG_SFF_POWER_LIMIT));
    cJSON_AddNumberToObject(root, "meanEfficiency", efficiency);
    cJSON_AddStringToObject(root, "sessionId", session);
}

/************************************************************************************************************
 * @brief Track BM1397 receive activity and restart after prolonged silence.
 * @param[in] g Non-NULL device state supplying ASIC type and operating status.
 * @param[in] received true when a valid ASIC result or register reply arrived;
 *                     false when checking the elapsed silence interval.
 * @return None; a triggered esp_restart() does not return.
 * @note Updates a private activity timestamp. Call only from the ASIC result
 *       task. Non-BM1397 devices are ignored. Inactive, paused, pool-unavailable,
 *       faulted, or self-test states refresh the timestamp. Board 2.A emits a
 *       health summary every 30 seconds while mining. Otherwise, more than ten
 *       minutes without valid packets logs an error and restarts.
 ***********************************************************************************************************/
void sff_result(GlobalState *g, bool received)
{
    if (g->DEVICE_CONFIG.family.asic.id != BM1397) return;
    int64_t now = esp_timer_get_time();
    bool stopped = !g->ASIC_initalized || g->SYSTEM_MODULE.mining_paused ||
                   g->SYSTEM_MODULE.pools_unavailable || g->SYSTEM_MODULE.hardware_fault ||
                   g->SELF_TEST_MODULE.is_active;
    if (received || stopped || last_result_us == 0) last_result_us = now;

    if (board_2a && !stopped && now - last_health_log_us >= 30000000LL) {
        uint32_t work_total;
        uint32_t job_total;
        uint32_t register_total;
        int64_t work_time;
        int64_t job_response_time;
        portENTER_CRITICAL(&info_lock);
        work_total = work_packets_sent;
        job_total = job_responses_received;
        register_total = register_responses_received;
        work_time = last_work_us;
        job_response_time = last_job_response_us;
        portEXIT_CRITICAL(&info_lock);

        uint32_t work_delta = work_total - health_work_packets_sent;
        uint32_t job_delta = job_total - health_job_responses_received;
        uint32_t register_delta = register_total - health_register_responses_received;
        float last_rx_seconds = (now - last_result_us) / 1000000.0f;
        float last_work_seconds = work_time > 0 ? (now - work_time) / 1000000.0f : -1.0f;
        size_t uart_buffered = 0;
        uint32_t uart_baud = 0;
        esp_err_t buffered_err = uart_get_buffered_data_len(UART_NUM_1, &uart_buffered);
        esp_err_t baud_err = uart_get_baudrate(UART_NUM_1, &uart_baud);
        PowerManagementModule *p = &g->POWER_MANAGEMENT_MODULE;

        bool no_replies = work_total > 0 && job_delta == 0 && register_delta == 0;
        bool zero_hashing = work_total > 0 && g->SYSTEM_MODULE.current_hashrate <= 0.0f &&
                            job_delta == 0;
        bool recent_job_result = job_response_time > 0 && now - job_response_time < 90000000LL;
        bool uart_stalled = work_total > 0 && now - last_result_us >= 90000000LL &&
                            work_time > 0 && now - work_time < 5000000LL;
        if (!zero_hashing || recent_job_result) {
            zero_hash_since_us = 0;
        } else if (zero_hash_since_us == 0) {
            zero_hash_since_us = now;
        }
        bool hashing_stalled = zero_hash_since_us > 0 && now - zero_hash_since_us >= 90000000LL;
        esp_log_level_t level = (no_replies || zero_hashing) ? ESP_LOG_WARN : ESP_LOG_INFO;
        ESP_LOG_LEVEL_LOCAL(level, "satsforfreedom",
                            "ASIC health: tx_work=%" PRIu32 " (+%" PRIu32 "), rx_job=%" PRIu32
                            " (+%" PRIu32 "), rx_reg=%" PRIu32 " (+%" PRIu32
                            "), last_rx=%.1fs, last_work=%.1fs, UART=%" PRIu32
                            " baud/%u buffered, hash=%.2f GH/s, freq=%.1f/%.1f MHz,"
                            " core=%.0f mV, input=%.0f mV, power=%.2f W, current=%.0f mA, temp=%.1f C",
                            work_total, work_delta, job_total, job_delta, register_total, register_delta,
                            last_rx_seconds, last_work_seconds,
                            baud_err == ESP_OK ? uart_baud : 0,
                            buffered_err == ESP_OK ? (unsigned)uart_buffered : 0,
                            g->SYSTEM_MODULE.current_hashrate, p->actual_frequency, p->frequency_value,
                            p->core_voltage, p->voltage, p->power, p->current, p->chip_temp_avg);

        if (no_replies) {
            ESP_LOGW("satsforfreedom",
                     "No valid ASIC UART replies in the last health interval despite work transmission");
        } else if (zero_hashing) {
            ESP_LOGW("satsforfreedom",
                     "ASIC register traffic is present, but no job results or measured hashrate were observed");
        }

        if (uart_stalled || hashing_stalled) {
            ESP_LOGE("satsforfreedom",
                     "ASIC failed to produce results for at least 90 seconds (%s); restarting system",
                     uart_stalled ? "UART replies stopped" : "zero hashrate");
            esp_restart();
        }

        health_work_packets_sent = work_total;
        health_job_responses_received = job_total;
        health_register_responses_received = register_total;
        last_health_log_us = now;
    }
    /* NULL also means an ignored packet. Use elapsed silence, not packet count. */
    if (!stopped && now - last_result_us > 600000000LL) {
        ESP_LOGE("satsforfreedom", "BM1397 silent for ten minutes; restarting");
        esp_restart();
    }
}

/************************************************************************************************************
 * @brief Read the smoothed mining efficiency for display.
 * @param[in] fallback Efficiency in W/TH/s to use before a sample is available.
 * @return The accumulated mean efficiency in W/TH/s, or fallback unchanged.
 * @note Reads shared statistics under info_lock; does not update the filter.
 ***********************************************************************************************************/
float sff_efficiency(float fallback)
{
    portENTER_CRITICAL(&info_lock);
    float value = efficiency_samples ? mean_efficiency : fallback;
    portEXIT_CRITICAL(&info_lock);
    return value;
}

/************************************************************************************************************
 * @brief Validate the SFF fields present in a partial settings update.
 * @param[in] root Non-NULL JSON object containing proposed settings.
 * @return false for an invalid powerLimitMilliwatts value (must be a finite
 *         integer from 0 to 15000), or for board 2.A voltage/frequency outside
 *         1000-1500 mV and 50-580 MHz; true otherwise, including absent fields.
 * @note Uses the cached board selection. Does not modify JSON or persist
 *       settings; upstream validation must still check all other fields.
 ***********************************************************************************************************/
bool sff_validate_settings(const cJSON *root)
{
    const cJSON *limit = cJSON_GetObjectItemCaseSensitive(root, "powerLimitMilliwatts");
    if (limit && (!cJSON_IsNumber(limit) || !isfinite(limit->valuedouble) ||
                  limit->valuedouble != limit->valueint || limit->valueint < 0 || limit->valueint > 15000)) return false;
    if (!board_2a) return true;
    const cJSON *voltage = cJSON_GetObjectItemCaseSensitive(root, "coreVoltage");
    const cJSON *frequency = cJSON_GetObjectItemCaseSensitive(root, "frequency");
    if (voltage && (!cJSON_IsNumber(voltage) || !isfinite(voltage->valuedouble) ||
                    voltage->valuedouble < 1000 || voltage->valuedouble > 1500)) return false;
    if (frequency && (!cJSON_IsNumber(frequency) || !isfinite(frequency->valuedouble) ||
                      frequency->valuedouble < 50 || frequency->valuedouble > 580)) return false;
    return true;
}
