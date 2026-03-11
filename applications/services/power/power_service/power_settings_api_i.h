#pragma once

#include "power_settings.h"
#include "power.h"

void power_api_get_settings(Power* instance, PowerSettings* settings);
void power_api_set_settings(Power* instance, const PowerSettings* settings);
