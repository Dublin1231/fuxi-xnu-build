/* SPDX-License-Identifier: BSD-2-Clause */
#include "vrm_voltage.h"
int fuxi_vrm_millivolts(uint64_t minimum,uint64_t maximum,uint32_t *out) {
    uint64_t mv;
    if(out)*out=0;
    if(!out || minimum>maximum)return ST_ARGUMENT;
    if(maximum>8191000)return ST_BLOCKED;
    mv=minimum/1000+(minimum%1000!=0);
    if(mv*1000>maximum)return ST_PROTOCOL;
    *out=(uint32_t)mv;return ST_OK;
}
