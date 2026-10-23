/* SPDX-License-Identifier: BSD-2-Clause */
#ifndef FUXI_VRM_VOLTAGE_H
#define FUXI_VRM_VOLTAGE_H
#include <stdint.h>
#include "../fuxi_st.h"
/* Unit conversion ONLY. No address, enable, mode or TCS transaction produced.
 * The source driver rounds minimum uV UP to mV and checks the upper bound.
 * The accelerator limit is not a board-safe voltage limit; caller must first
 * intersect board, consumer, regulator and shared ownership constraints.
 */
int fuxi_vrm_millivolts(uint64_t,uint64_t,uint32_t *);
#endif
