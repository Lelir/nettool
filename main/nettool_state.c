#include "nettool_state.h"

static nettool_state_t g_state = {0};

nettool_state_t *nettool_state_get(void)
{
    return &g_state;
}
