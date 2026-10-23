/* SPDX-License-Identifier: BSD-2-Clause */
#ifndef FUXI_ST_GENI_H
#define FUXI_ST_GENI_H
#include "fuxi_st.h"
#include "GeniSpi.h"
struct st_geni_transport { struct geni_spi *spi; int faulted; };
int fuxi_st_geni_exchange(void *ctx, uint8_t command, uint8_t *rx, size_t len);
#endif
