/* SPDX-License-Identifier: BSD-2-Clause
 * Independent bounded decoder of the documented byte layout. Source provenance
 * in references/provider-v1; this file does not port Linux driver code.
 */
#include "cmddb.h"
#define DB_BASE 144u
#define ENTRY_SIZE 24u
static uint16_t u16(const uint8_t *p) {return (uint16_t)((unsigned)p[0]|((unsigned)p[1]<<8));}
static uint32_t u32(const uint8_t *p) {return (uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);}
static int same8(const uint8_t *a,const uint8_t *b) {
    unsigned i;for(i=0;i<8;i++)if(a[i]!=b[i])return 0;return 1;
}
static int span(size_t start,size_t count,size_t length) {return start<=length && count<=length-start;}
static int overlap(size_t a,size_t an,size_t b,size_t bn) {return an && bn && a<b+bn && b<a+an;}
int fuxi_cmddb_lookup(const uint8_t *data,size_t length,uint32_t version,
                     const uint8_t *query,size_t query_length,struct fuxi_cmddb_resource *out) {
    size_t table[8]={0},counts[8]={0},aux[8]={0},entries[FUXI_CMDDB_MAX_ENTRIES];
    unsigned types[8]={0},versions[8]={0},i,j,k,ended=0,total=0,found=0;
    uint8_t key[8]={0};struct fuxi_cmddb_resource result={0};
    if(out){struct fuxi_cmddb_resource blank={0};*out=blank;}
    if(!data || !out || !query || !query_length || query_length>8)return ST_ARGUMENT;
    for(i=0;i<query_length;i++){if(!query[i])return ST_ARGUMENT;key[i]=query[i];}
    if(length<DB_BASE)return ST_PROTOCOL;
    if(length>FUXI_CMDDB_MAX_BYTES || !version)return ST_BLOCKED;
    if(data[4]!=0xdb || data[5]!=0x30 || data[6]!=0x03 || data[7]!=0x0c)return ST_PROTOCOL;
    if(u32(data)!=version)return ST_BLOCKED;
    for(i=0;i<8;i++) {
        const uint8_t *header=data+8+16*i;
        types[i]=u16(header);counts[i]=u16(header+6);versions[i]=u16(header+8);
        if(!types[i]){ended=1;for(j=0;j<16;j++)if(header[j])return ST_PROTOCOL;continue;}
        if(ended)return ST_PROTOCOL;
        if(types[i]<3 || types[i]>5)return ST_BLOCKED;
        for(j=0;j<i;j++)if(types[j]==types[i])return ST_PROTOCOL;
        if(counts[i]>FUXI_CMDDB_MAX_ENTRIES-total)return ST_BLOCKED;
        total+=(unsigned)counts[i];
        table[i]=DB_BASE+u16(header+2);aux[i]=DB_BASE+u16(header+4);
        if(!span(table[i],counts[i]*ENTRY_SIZE,length) || !span(aux[i],0,length))return ST_PROTOCOL;
        for(j=0;j<i;j++)if(overlap(table[i],counts[i]*ENTRY_SIZE,table[j],counts[j]*ENTRY_SIZE))return ST_PROTOCOL;
    }
    total=0;
    for(i=0;i<8 && types[i];i++)for(j=0;j<counts[i];j++) {
        size_t entry=table[i]+j*ENTRY_SIZE;
        const uint8_t *p=data+entry;
        size_t start=aux[i]+u16(p+22),n=u16(p+20);
        uint32_t address=u32(p+16);
        if(!address || (address&3) || ((address>>16)&7)!=types[i])return ST_PROTOCOL;
        if(!span(start,n,length))return ST_PROTOCOL;
        for(k=0;k<8 && types[k];k++)if(overlap(start,n,table[k],counts[k]*ENTRY_SIZE))return ST_PROTOCOL;
        for(k=0;k<total;k++)if(same8(p,data+entries[k]))return ST_PROTOCOL;
        entries[total++]=entry;
        if(same8(p,key)) {
            result.address=address;result.slave=(uint16_t)types[i];result.slave_version=(uint16_t)versions[i];
            result.aux_offset=start;result.aux_length=n;found=1;
        }
    }
    if(!found)return ST_BLOCKED;
    result.standalone=u32(data+140)&1;*out=result;return ST_OK;
}
