/* SPDX-License-Identifier: BSD-2-Clause */
#include "fuxi_st_geni.h"
int fuxi_st_geni_exchange(void *ctx, uint8_t command, uint8_t *rx, size_t len) {
    struct st_geni_transport *t=(struct st_geni_transport *)ctx;
    int rc;
    if(!t || !t->spi || !rx || len<9 || len>249 || ((len-1)%8) || command!=0x87)
        return ST_ARGUMENT;
    if(t->faulted) return ST_IO;
    /* GENI fragmentation keeps CS asserted across command and receive. No
     * manual delay between phases: local module constructs one spi_message.
     * Actual GENI CS/timing/DMA behavior still needs hardware qualification. */
    rc=geni_spi_transfer(t->spi,&command,0,1,1);
    if(!rc) rc=geni_spi_transfer(t->spi,0,rx,len,0);
    if(rc) { t->faulted=1; return ST_IO; }
    return ST_OK;
}
