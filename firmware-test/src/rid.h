#pragma once
#include <Arduino.h>

void ridBegin();
void ridPoll();
void ridStats(uint32_t &frames, uint32_t &adverts, uint32_t &messages);
