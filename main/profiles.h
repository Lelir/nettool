#pragma once
#include <stddef.h>
#include <stdint.h>

typedef enum {
    PROFILE_GENERIC_PC = 0,
    PROFILE_HP_PRINTER,
    PROFILE_RICOH_PRINTER,
    PROFILE_CISCO_AP,
    PROFILE_CUSTOM,
} profile_id_t;

typedef struct {
    profile_id_t id;
    const char *name;
    const char *vendor;
} device_profile_t;

const device_profile_t *profiles_get(size_t *count);

// V0 only: safe locally-administered MAC generation.
// Vendor-OUI impersonation will be added as an explicit test-lab option later.
void profiles_make_local_mac(uint8_t out[6], uint32_t seed);
