#ifndef SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_INTEL_IWLWIFI_PLATFORM_MEMORY_H_
#define SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_INTEL_IWLWIFI_PLATFORM_MEMORY_H_

// This file contains system calls related to memory management.

#include <stdint.h>
#include <zircon/types.h>

#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/platform/kernel.h"

#if defined(__cplusplus)
extern "C" {
#endif  // defined(__cplusplus)

// A struct representing a device-visible memory buffer.
struct iwl_iobuf;

// Initialize a physically contiguous memory buffer.
zx_status_t iwl_iobuf_allocate_contiguous(struct device* dev, size_t size,
                                          struct iwl_iobuf** out_iobuf);

// Get the size of an iwl_iobuf.
size_t iwl_iobuf_size(const struct iwl_iobuf* iobuf);

// Get the virtual address of an iwl_iobuf.
void* iwl_iobuf_virtual(const struct iwl_iobuf* iobuf);

// Get the physical address of an iwl_iobuf.
dma_addr_t iwl_iobuf_physical(const struct iwl_iobuf* iobuf);

// Perform a cache flush on a range of an iwl_iobuf.
zx_status_t iwl_iobuf_cache_flush(struct iwl_iobuf* iobuf, size_t offset, size_t size);

// Release an iwl_iobuf.
void iwl_iobuf_release(struct iwl_iobuf* iobuf);

#if defined(__cplusplus)
}  // extern "C"
#endif  // defined(__cplusplus)

#endif  // SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_INTEL_IWLWIFI_PLATFORM_MEMORY_H_
