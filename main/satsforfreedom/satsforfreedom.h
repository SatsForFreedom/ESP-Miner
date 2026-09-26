#ifndef SATSFORFREEDOM_H
#define SATSFORFREEDOM_H
#include "esp_err.h"
#include "cJSON.h"
#include <stdbool.h>
typedef struct GlobalState GlobalState;

/************************************************************************************************************
 * @brief Report whether board 2.A is the selected device.
 * @return true after sff_configure() identifies board 2.A; false otherwise.
 * @note Reads cached startup state and has no side effects.
 ***********************************************************************************************************/
bool sff_board(void);

/************************************************************************************************************
 * @brief Select the ASIC regulator-enable pin for the active board.
 * @param[in] default_pin Pin used by boards other than 2.A.
 * @return GPIO14 for board 2.A, otherwise default_pin.
 * @note Does not configure or drive the pin.
 ***********************************************************************************************************/
int sff_enable_pin(int default_pin);

/************************************************************************************************************
 * @brief Apply board-specific hardware settings during device initialization.
 * @param[in,out] g Device state whose board_version has already been set.
 * @return None.
 * @note Caches board selection and configures the board 2.A BI signal on GPIO10.
 *       Call once before ADC initialization and worker tasks start.
 ***********************************************************************************************************/
void sff_configure(GlobalState *g);

/************************************************************************************************************
 * @brief Read the TPS40305 PGOOD signal on board 2.A.
 * @return ESP_OK when PGOOD is high or the selected board is not 2.A; ESP_FAIL
 *         when board 2.A reports PGOOD low.
 * @note Configures GPIO11 as an input and logs the result. The caller may log
 *       a failure without aborting startup, as the original firmware did.
 ***********************************************************************************************************/
esp_err_t sff_check_pgood(void);

/************************************************************************************************************
 * @brief Verify the board 2.A core voltage before ASIC initialization.
 * @param[in,out] g Device state used for the voltage target and fault status.
 * @return ESP_OK for non-2.A boards or a valid voltage; ESP_FAIL on mismatch.
 * @note Requires initialized ADC, NVS, and regulator interfaces. Blocks for
 *       about one second on board 2.A; on failure requests regulator shutdown
 *       and records a hardware fault.
 ***********************************************************************************************************/
esp_err_t sff_check_voltage(GlobalState *g);

/************************************************************************************************************
 * @brief Calculate the next BM1397 frequency under the configured power target.
 * @param[in] g Device state containing ASIC status and current telemetry.
 * @param[in] maximum Configured frequency ceiling, in MHz.
 * @return Next frequency target in MHz; the active control range is 50-580 MHz.
 * @note Reads the NVS limit and updates private power and efficiency filters.
 *       Call from the power-management task. Does not apply frequency or replace
 *       the upstream thermal protection.
 ***********************************************************************************************************/
float sff_frequency(GlobalState *g, float maximum);

/************************************************************************************************************
 * @brief Read the smoothed mining-efficiency value for display.
 * @param[in] fallback Efficiency in W/TH/s to use before a sample is available.
 * @return Smoothed efficiency, or fallback when no sample is available.
 ***********************************************************************************************************/
float sff_efficiency(float fallback);

/************************************************************************************************************
 * @brief Validate SFF power, voltage, and frequency fields in a settings update.
 * @param[in] root JSON object containing a partial settings update.
 * @return false for an invalid SFF value; true if present SFF fields are valid.
 * @note Does not persist settings or validate unrelated fields.
 ***********************************************************************************************************/
bool sff_validate_settings(const cJSON *root);

/************************************************************************************************************
 * @brief Append SFF power-limit and session telemetry to a JSON object.
 * @param[in,out] root JSON object that receives the telemetry fields.
 * @param[in] g Reserved device-state parameter; currently unused.
 * @return None.
 * @note Reads NVS and shared efficiency/session state. The JSON object owns
 *       newly allocated cJSON fields; call once per object.
 ***********************************************************************************************************/
void sff_add_info(cJSON *root, GlobalState *g);

/************************************************************************************************************
 * @brief Replace the cached Stratum V1 session identifier.
 * @param[in] session NUL-terminated identifier to copy, or NULL to clear it.
 * @return None.
 * @note Copies at most 128 characters under a critical section; caller retains
 *       ownership of the input string.
 ***********************************************************************************************************/
void sff_session(const char *session);

/************************************************************************************************************
 * @brief Track BM1397 receive activity and check the prolonged-silence watchdog.
 * @param[in] g Device state used to determine whether mining is active.
 * @param[in] received true when a valid ASIC packet arrived, false to check time.
 * @return None; esp_restart() is called after ten minutes of active silence.
 * @note Call from the ASIC result task. Paused, faulted, or self-test states
 *       suppress the watchdog.
 ***********************************************************************************************************/
void sff_result(GlobalState *g, bool received);
#endif
