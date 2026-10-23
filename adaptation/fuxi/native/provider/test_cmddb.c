/* SPDX-License-Identifier: BSD-2-Clause */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cmddb.h"
#include "vrm_voltage.h"
static unsigned assertions,scenarios;
#define CHECK(x) do{assertions++;if(!(x)){fprintf(stderr,"line %u: %s\n",__LINE__,#x);exit(1);}}while(0)
static void w16(uint8_t *p,unsigned n){p[0]=(uint8_t)n;p[1]=(uint8_t)(n>>8);}
static void w32(uint8_t *p,unsigned n){w16(p,n);w16(p+2,n>>16);}
/* Synthetic addresses and version, NOT a captured fuxi database. */
static void fixture(uint8_t *b) {
    memset(b,0,256);w32(b,1);b[4]=0xdb;b[5]=0x30;b[6]=3;b[7]=0x0c;
    w16(b+8,4);w16(b+10,0);w16(b+12,48);w16(b+14,2);w16(b+16,0x100);
    memcpy(b+144,"ldom6",5);w32(b+160,0x40010);w16(b+164,2);
    memcpy(b+168,"ldob12",6);w32(b+184,0x40020);w16(b+188,2);w16(b+190,2);
    b[192]=1;b[193]=2;b[194]=3;b[195]=4;
}
static int lookup(const uint8_t *b,size_t n,struct fuxi_cmddb_resource *r){return fuxi_cmddb_lookup(b,n,1,(const uint8_t *)"ldom6",5,r);}
int main(void) {
    uint8_t b[256],unaligned[257];struct fuxi_cmddb_resource r;
    fixture(b);scenarios++;CHECK(lookup(b,196,&r)==ST_OK);
    CHECK(r.address==0x40010 && r.slave==4 && r.slave_version==0x100 && r.aux_offset==192 && r.aux_length==2 && !r.standalone);
    CHECK(fuxi_cmddb_lookup(b,196,1,(const uint8_t *)"ldob12",6,&r)==ST_OK && r.address==0x40020);
    scenarios++;for(size_t n=0;n<196;n++){CHECK(lookup(b,n,&r)!=ST_OK);CHECK(r.address==0);}
    scenarios++;CHECK(lookup(NULL,196,&r)==ST_ARGUMENT);CHECK(lookup(b,196,NULL)==ST_ARGUMENT);
    CHECK(fuxi_cmddb_lookup(b,196,0,(const uint8_t *)"ldom6",5,&r)==ST_BLOCKED);
    CHECK(fuxi_cmddb_lookup(b,196,2,(const uint8_t *)"ldom6",5,&r)==ST_BLOCKED);
    CHECK(fuxi_cmddb_lookup(b,196,1,(const uint8_t *)"ldom6",0,&r)==ST_ARGUMENT);
    CHECK(fuxi_cmddb_lookup(b,196,1,(const uint8_t *)"ldom6xxxx",9,&r)==ST_ARGUMENT);
    CHECK(fuxi_cmddb_lookup(b,196,1,(const uint8_t *)"ld\0m6",5,&r)==ST_ARGUMENT);
    CHECK(fuxi_cmddb_lookup(b,196,1,(const uint8_t *)"absent",6,&r)==ST_BLOCKED && !r.address);
    scenarios++;b[4]=0;CHECK(lookup(b,196,&r)==ST_PROTOCOL);fixture(b);
    scenarios++;w16(b+10,65535);CHECK(lookup(b,196,&r)==ST_PROTOCOL);fixture(b);
    scenarios++;w16(b+12,65535);CHECK(lookup(b,196,&r)==ST_PROTOCOL);fixture(b);
    scenarios++;w16(b+190,65535);CHECK(lookup(b,196,&r)==ST_PROTOCOL);fixture(b);
    scenarios++;w16(b+14,513);CHECK(lookup(b,196,&r)==ST_BLOCKED);fixture(b);
    scenarios++;memcpy(b+168,b+144,8);CHECK(lookup(b,196,&r)==ST_PROTOCOL);fixture(b);
    scenarios++;w32(b+184,0x40021);CHECK(lookup(b,196,&r)==ST_PROTOCOL);fixture(b);
    scenarios++;w32(b+184,0);CHECK(lookup(b,196,&r)==ST_PROTOCOL);fixture(b);
    scenarios++;w32(b+184,0x50020);CHECK(lookup(b,196,&r)==ST_PROTOCOL);fixture(b);
    scenarios++;w16(b+12,0);CHECK(lookup(b,196,&r)==ST_PROTOCOL);fixture(b);
    scenarios++;w16(b+40,5);CHECK(lookup(b,196,&r)==ST_PROTOCOL);fixture(b);
    scenarios++;w16(b+24,4);CHECK(lookup(b,196,&r)==ST_PROTOCOL);fixture(b);
    scenarios++;w16(b+24,5);w16(b+30,1);CHECK(lookup(b,196,&r)==ST_PROTOCOL);fixture(b);
    scenarios++;w16(b+8,6);CHECK(lookup(b,196,&r)==ST_BLOCKED);fixture(b);
    scenarios++;b[140]=1;CHECK(lookup(b,196,&r)==ST_OK && r.standalone);fixture(b);
    scenarios++;CHECK(lookup(b,FUXI_CMDDB_MAX_BYTES+1,&r)==ST_BLOCKED);
    scenarios++;memcpy(unaligned+1,b,256);CHECK(lookup(unaligned+1,196,&r)==ST_OK);
    scenarios++;memcpy(b+144,"12345678",8);
    CHECK(fuxi_cmddb_lookup(b,196,1,(const uint8_t *)"12345678",8,&r)==ST_OK);
    CHECK(lookup(b,196,&r)==ST_BLOCKED);fixture(b);
    /* Deterministic malformed full-buffer mutation corpus. */
    scenarios++;uint32_t seed=0x35137u;
    for(unsigned i=0;i<10000;i++) {
        fixture(b);seed=1664525u*seed+1013904223u;unsigned at=seed%196;
        seed=1664525u*seed+1013904223u;b[at]^=(uint8_t)(1+(seed%255));
        int rc=lookup(b,196,&r);
        CHECK(rc==ST_OK || rc==ST_PROTOCOL || rc==ST_BLOCKED);
        if(rc==ST_OK)CHECK(r.address && r.aux_offset<=196 && r.aux_length<=196-r.aux_offset);
        else CHECK(r.address==0 && r.aux_length==0);
    }
    scenarios++;uint32_t mv;
    CHECK(fuxi_vrm_millivolts(1800000,1800000,&mv)==ST_OK && mv==1800);
    CHECK(fuxi_vrm_millivolts(1800001,1801000,&mv)==ST_OK && mv==1801);
    CHECK(fuxi_vrm_millivolts(1800001,1800999,&mv)==ST_PROTOCOL && mv==0);
    CHECK(fuxi_vrm_millivolts(3200000,1800000,&mv)==ST_ARGUMENT);
    CHECK(fuxi_vrm_millivolts(8191000,8191000,&mv)==ST_OK && mv==8191);
    CHECK(fuxi_vrm_millivolts(8191001,8191001,&mv)==ST_BLOCKED && mv==0);
    CHECK(fuxi_vrm_millivolts(0,UINT64_MAX,&mv)==ST_BLOCKED);
    CHECK(fuxi_vrm_millivolts(0,0,&mv)==ST_OK && mv==0);
    CHECK(fuxi_vrm_millivolts(0,0,NULL)==ST_ARGUMENT);
    printf("{\"scenarios\":%u,\"assertions\":%u,\"mutation_vectors\":10000,\"hardware_tested\":false}\n",scenarios,assertions);
    return 0;
}
