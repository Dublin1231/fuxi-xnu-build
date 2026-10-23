/* SPDX-License-Identifier: BSD-2-Clause
 * Source-corroborated constants, NOT hardware qualification or an MMIO backend.
 * Evidence: references/kalama-v2 at f9ca2b71f6725a0d4f0b1cb2aa553baaea98e74d.
 */
#ifndef FUXI_KALAMA_REGISTERS_H
#define FUXI_KALAMA_REGISTERS_H
#include <stdint.h>
#define ST_TLMM_WINDOW 0x0f000000u
#define ST_TLMM_TILE_OFFSET 0x00100000u
#define ST_TLMM_GPIO_STRIDE 0x1000u
#define ST_TLMM_QUP_MUX 1u
#define ST_GCC_WINDOW 0x00100000u
#define ST_GCC_QUP_VOTE_OFFSET 0x52008u
#define ST_GCC_S4_HALT_OFFSET 0x184e4u
#define ST_GCC_M_AHB_HALT_OFFSET 0x2327cu
#define ST_GCC_S_AHB_HALT_OFFSET 0x23280u
#define ST_GCC_S4_RCG_OFFSET 0x184f0u
#define ST_GCC_S4_VOTE (1u<<26)
#define ST_GCC_M_AHB_VOTE (1u<<20)
#define ST_GCC_S_AHB_VOTE (1u<<21)
#define ST_SPI_MAX_HZ 12000000u
enum st_transport_mode { ST_TRANSPORT_PIO,ST_TRANSPORT_SE_DMA,ST_TRANSPORT_GPI };
/* spi-msm-geni select_xfer_mode does NOT qualify SE DMA when FIFO_IF_DISABLE=1.
 * GPI is not implemented here. No speculative sheng-style bypass fallback. */
static inline int st_transport_policy(unsigned protocol,unsigned fifo_disabled,
                                       enum st_transport_mode mode,unsigned geometry_valid,
                                       unsigned dma_mapping_qualified) {
    if(protocol!=1 || fifo_disabled || !geometry_valid || mode==ST_TRANSPORT_GPI)return -1;
    if(mode==ST_TRANSPORT_SE_DMA)return dma_mapping_qualified?0:-1;
    return mode==ST_TRANSPORT_PIO?0:-1;
}
static inline uint32_t st_gpio_ctl_address(unsigned gpio) {
    if(gpio>=211)return 0;
    return ST_TLMM_WINDOW+ST_TLMM_TILE_OFFSET+gpio*ST_TLMM_GPIO_STRIDE;
}
/* Inputs must be measured/qualified parent rates, never assumed PLL frequency. */
static inline int st_clock_divider(uint64_t source,unsigned oversampling,unsigned target,
                                    unsigned *divider,uint64_t *actual) {
    uint64_t denominator,d;
    if(!source || !oversampling || !target || target>ST_SPI_MAX_HZ || !divider || !actual)return -1;
    denominator=(uint64_t)oversampling*target;
    d=source/denominator+(source%denominator!=0);
    if(!d || d>4095)return -1;
    *divider=(unsigned)d;*actual=source/(oversampling*d);return *actual?0:-1;
}
#endif
