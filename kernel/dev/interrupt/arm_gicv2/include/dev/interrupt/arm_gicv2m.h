// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2016, Google Inc. All rights reserved.
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <magenta/compiler.h>
#include <sys/types.h>

__BEGIN_CDECLS

/**
 * Structure used to hold information about a GICv2m register frame
 * @see arm_gicv2m_get_frame_info
 */
typedef struct arm_gicv2m_frame_info {
    uint start_spi_id;  /** The first valid SPI ID in the frame */
    uint end_spi_id;    /** The last valid SPI ID in the frame */
    paddr_t doorbell;   /** The physical address of the doorbell register */
    uint32_t iid;       /** The value of the Interface ID register */
} arm_gicv2m_frame_info_t;

/**
 * Support for the MSI extensions to the GICv2 architecture.  See the ARM Server
 * Base System Architecture v3.0 (ARM_DEN_0029) Appendix E for details.
 *
 * @param reg_frames An array of physical addresses of the 4k V2M register
 * frames implemented by this platform's GIC.  Note: The memory backing this
 * array must be alive for the lifetime of the system.
 * @param reg_frame_count The number of entries in the reg_frames array.
 */
void arm_gicv2m_init(const paddr_t* reg_frames, const vaddr_t* reg_frames_virt, uint reg_frame_count);

/**
 * Fetch info about a specific GICv2m register frame
 *
 * @param frame_num The index of the frame to fetch info for
 * @param out_info A pointer to the structure which will hold info about the frame
 * @return A status code indicating the success or failure of the operation.
 * Status codes may include...
 *  ++ MX_ERR_UNAVAILABLE The GICv2m subsystem was never initialized
 *  ++ MX_ERR_NOT_FOUND frame_ndx is out of range
 *  ++ MX_ERR_INVALID_ARGS out_info is NULL
 *  ++ MX_ERR_BAD_STATE The frame index exists, but the registers in the frame
 *     appear to be corrupt or invalid (internal error)
 */
status_t arm_gicv2m_get_frame_info(uint frame_ndx, arm_gicv2m_frame_info_t* out_info);

__END_CDECLS
