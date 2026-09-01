#include "profiles.h"

static const device_profile_t s_profiles[] = {
    { PROFILE_GENERIC_PC,   "PC générique",     "Generic" },
    { PROFILE_HP_PRINTER,   "Imprimante HP",    "HP" },
    { PROFILE_RICOH_PRINTER,"Imprimante Ricoh", "Ricoh" },
    { PROFILE_CISCO_AP,     "Cisco AP",          "Cisco" },
    { PROFILE_CUSTOM,       "Personnalisé",      "Custom" },
};

const device_profile_t *profiles_get(size_t *count)
{
    if (count) {
        *count = sizeof(s_profiles) / sizeof(s_profiles[0]);
    }
    return s_profiles;
}

void profiles_make_local_mac(uint8_t out[6], uint32_t seed)
{
    // x2 bit = locally administered, multicast bit stays 0
    out[0] = 0x02;
    out[1] = 0x4E; // 'N'
    out[2] = 0x54; // 'T'
    out[3] = (seed >> 16) & 0xFF;
    out[4] = (seed >> 8) & 0xFF;
    out[5] = seed & 0xFF;
}
