/* SPDX-License-Identifier: BSD-2-Clause */
#include "fuxi_hid_reports.h"
#include <string.h>
#include <stdio.h>
#define CHECK(e) do { if(!(e)) {fprintf(stderr,"CHECK failed at line %d\n",__LINE__);return 1;} } while(0)
int main(void) {
    struct st_state state;struct st_reports reports;uint8_t events[32][8]={{0x10,0x01,1,0,2,0}};
    fuxi_st_init(&state);CHECK(fuxi_st_decode(&state,events,1,&reports)==ST_OK);
    CHECK(fuxi_hid_reports_valid(&reports.bytes[0][0],62,reports.count));
    CHECK(fuxi_st_cancel(&state,&reports)==ST_OK);
    CHECK(reports.bytes[0][1]==2 && fuxi_hid_reports_valid(&reports.bytes[0][0],62,1));
    uint8_t valid[124]={3};valid[1]=3;valid[2]=9;valid[61]=1;
    memcpy(valid+62,valid,62);CHECK(fuxi_hid_reports_valid(valid,sizeof(valid),2));
    CHECK(!fuxi_hid_reports_valid(valid,123,2));CHECK(!fuxi_hid_reports_valid(valid,124,33));
    CHECK(!fuxi_hid_reports_valid(0,62,1));CHECK(!fuxi_hid_reports_valid(valid,0,0));
    const unsigned offsets[]={62,63,64,66,68,123};
    const uint8_t values[]={2,1,10,128,128,11};
    for(unsigned i=0;i<6;i++) {
        uint8_t broken[124];memcpy(broken,valid,sizeof(broken));broken[offsets[i]]=values[i];
        CHECK(!fuxi_hid_reports_valid(broken,sizeof(broken),2));
    }
    uint8_t duplicate[62];memcpy(duplicate,valid,62);duplicate[61]=2;
    memcpy(duplicate+7,duplicate+1,6);CHECK(!fuxi_hid_reports_valid(duplicate,62,1));
    valid[9]=1;CHECK(!fuxi_hid_reports_valid(valid,sizeof(valid),2));
    puts("HID report contract: decoder, tip-up, malformed later report, duplicates, bounds PASS; hardware_tested=false");
    return 0;
}
