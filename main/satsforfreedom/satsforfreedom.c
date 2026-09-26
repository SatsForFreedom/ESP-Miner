#include "satsforfreedom.h"
#include "control.h"
#include "global_state.h"
#include "nvs_config.h"
#include "vcore.h"
#include "adc.h"
#include "esp_timer.h"
#include "esp_system.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "freertos/task.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>

static bool board_2a;
static float mean_efficiency;
static unsigned efficiency_samples;
static float filtered_power;
static bool power_sampled;
static int64_t last_result_us;
static char session_id[129];
static portMUX_TYPE info_lock = portMUX_INITIALIZER_UNLOCKED;

bool sff_board(void) { return board_2a; }
int sff_enable_pin(int default_pin) { return board_2a ? 14 : default_pin; }

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

void sff_session(const char *session)
{
    portENTER_CRITICAL(&info_lock);
    snprintf(session_id, sizeof(session_id), "%s", session ? session : "");
    portEXIT_CRITICAL(&info_lock);
}

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

void sff_result(GlobalState *g, bool received)
{
    if (g->DEVICE_CONFIG.family.asic.id != BM1397) return;
    int64_t now = esp_timer_get_time();
    bool stopped = !g->ASIC_initalized || g->SYSTEM_MODULE.mining_paused ||
                   g->SYSTEM_MODULE.pools_unavailable || g->SYSTEM_MODULE.hardware_fault ||
                   g->SELF_TEST_MODULE.is_active;
    if (received || stopped || last_result_us == 0) last_result_us = now;
    /* NULL also means an ignored packet. Use elapsed silence, not packet count. */
    if (!stopped && now - last_result_us > 600000000LL) {
        ESP_LOGE("satsforfreedom", "BM1397 silent for ten minutes; restarting");
        esp_restart();
    }
}

float sff_efficiency(float fallback)
{
    portENTER_CRITICAL(&info_lock);
    float value = efficiency_samples ? mean_efficiency : fallback;
    portEXIT_CRITICAL(&info_lock);
    return value;
}

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
