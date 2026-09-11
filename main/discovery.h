#pragma once

#include <stddef.h>
#include <stdint.h>

void discovery_process_frame(const uint8_t *frame, size_t len);
void discovery_clear(void);
