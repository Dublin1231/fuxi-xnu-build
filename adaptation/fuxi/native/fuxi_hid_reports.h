/* SPDX-License-Identifier: BSD-2-Clause */
#ifndef FUXI_HID_REPORTS_H
#define FUXI_HID_REPORTS_H
#include "fuxi_st.h"
/* Validate the whole borrowed batch before allocating or delivering any report.
 * Contact Count includes explicit tip-up rows. Unused rows must be zero. */
static inline int fuxi_hid_reports_valid(const uint8_t *bytes,size_t length,size_t count) {
    if(!bytes || !count || count>FUXI_ST_DEPTH || length!=count*FUXI_ST_REPORT_BYTES)return 0;
    for(size_t i=0;i<count;i++) {
        const uint8_t *r=bytes+i*FUXI_ST_REPORT_BYTES;unsigned seen=0;
        if(r[0]!=3 || r[61]>FUXI_ST_CONTACTS)return 0;
        for(unsigned j=0;j<FUXI_ST_CONTACTS;j++) {
            const uint8_t *p=r+1+6*j;
            if(j>=r[61]) {for(unsigned k=0;k<6;k++)if(p[k])return 0;continue;}
            if((p[0]!=2 && p[0]!=3) || p[1]>=FUXI_ST_CONTACTS || (seen&(1u<<p[1])))return 0;
            seen|=1u<<p[1];
            if((p[3]&0x80) || (p[5]&0x80))return 0;
        }
    }
    return 1;
}
#endif
