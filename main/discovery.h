#pragma once
#include <stddef.h>
#include <stdint.h>

// Parsers are intentionally separated from capture.
// Once RMII is validated, raw Ethernet frames can be fed here.
void discovery_process_frame(const uint8_t *frame, size_t len);
