#ifndef SATSFORFREEDOM_H
#define SATSFORFREEDOM_H
#include "esp_err.h"
#include "cJSON.h"
#include <stdbool.h>
typedef struct GlobalState GlobalState;
bool sff_board(void);
int sff_enable_pin(int default_pin);
void sff_configure(GlobalState *g);
esp_err_t sff_check_voltage(GlobalState *g);
float sff_frequency(GlobalState *g, float maximum);
float sff_efficiency(float fallback);
bool sff_validate_settings(const cJSON *root);
void sff_add_info(cJSON *root, GlobalState *g);
void sff_session(const char *session);
void sff_result(GlobalState *g, bool received);
#endif
