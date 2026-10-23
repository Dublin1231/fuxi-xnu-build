/* Test-only link replacement. Never included in native builds. */
#include "GeniSpi.h"
unsigned mock_calls,mock_fail_at;
size_t mock_len[4]; int mock_keep[4],mock_is_rx[4]; uint8_t mock_cmd;
int geni_spi_transfer(struct geni_spi *s,const uint8_t *tx,uint8_t *rx,size_t len,int keep) {
    unsigned i=mock_calls++; size_t j; (void)s;
    if(i>=4) return -1;
    mock_len[i]=len; mock_keep[i]=keep; mock_is_rx[i]=(rx!=0);
    if(tx) mock_cmd=tx[0];
    if(mock_fail_at==i+1) return -1;
    if(rx) for(j=0;j<len;j++) rx[j]=(uint8_t)j;
    return 0;
}
