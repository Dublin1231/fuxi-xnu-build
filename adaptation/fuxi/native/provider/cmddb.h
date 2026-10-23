/* SPDX-License-Identifier: BSD-2-Clause */
#ifndef FUXI_CMDDB_H
#define FUXI_CMDDB_H
#include <stddef.h>
#include <stdint.h>
#include "../fuxi_st.h"
#ifdef __cplusplus
extern "C" {
#endif
/* Conservative parser policy, NOT a claimed hardware capacity. */
#define FUXI_CMDDB_MAX_ENTRIES 512u
#define FUXI_CMDDB_MAX_BYTES 1048576u
struct fuxi_cmddb_resource {
    uint32_t address;
    uint16_t slave, slave_version;
    size_t aux_offset, aux_length;
    unsigned standalone;
};
/* Parse an immutable, already captured byte buffer, never mapped MMIO.
 * expected_version must come from independent qualified provenance, not data[0].
 * The entire visible DB is validated before any result is returned.
 * Success means structural lookup ONLY, not authenticity, ownership or readiness.
 * Returns ST_BLOCKED for unsupported version/type/capacity or missing resource.
 * No struct casts, dynamic allocation, writes, checksum/authenticity assertion.
 * Caller must not overlap out with input; input remains immutable during call.
 */
int fuxi_cmddb_lookup(const uint8_t *,size_t,uint32_t,
                     const uint8_t *,size_t,struct fuxi_cmddb_resource *);
#ifdef __cplusplus
}
#endif
#endif
