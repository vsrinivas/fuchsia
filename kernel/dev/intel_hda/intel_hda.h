// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <magenta/compiler.h>
#include <dev/pcie.h>
#include <sys/types.h>
#include <list.h>

#include "codec.h"
#include "registers.h"

__BEGIN_CDECLS

typedef struct intel_hda_device {
    struct list_node     device_list_node;
    struct list_node     pending_work_list_node;
    int                  ref_count;
    int                  dev_id;
    pcie_device_state_t* pci_device;

    /* Mapped registers */
    hda_registers_t*        regs;
    hda_stream_desc_regs_t* input_strm_regs;
    hda_stream_desc_regs_t* output_strm_regs;
    hda_stream_desc_regs_t* bidir_strm_regs;
    size_t                  input_strm_cnt;
    size_t                  output_strm_cnt;
    size_t                  bidir_strm_cnt;

    /* Codec Command TX/RX queue state. */
    struct list_node        codec_cmd_buf_pages;

    hda_corb_entry_t*       corb;
    uint                    corb_entry_count;
    uint                    corb_mask;
    uint                    corb_wr_ptr;
    uint                    corb_snapshot_space;
    uint                    corb_max_in_flight;

    hda_rirb_entry_t*       rirb;
    uint                    rirb_entry_count;
    uint                    rirb_mask;
    uint                    rirb_rd_ptr;
    uint                    rirb_snapshot_cnt;
    hda_rirb_entry_t        rirb_snapshot[HDA_RIRB_MAX_ENTRIES];

    /* Codec State */
    intel_hda_codec_t*      codecs[INTEL_HDA_MAX_CODECS];
} intel_hda_device_t;

/**
 * Callback used when iterating the list of active Intel HDA audio devices.
 */
typedef void (*intel_hda_foreach_cbk)(intel_hda_device_t* dev, void* ctx);

/**
 * Iterate over the current list of Intel HDA audio devices calling 'cbk' (and
 * supplying ctx in the process) once for each active device.
 *
 * @note Use with caution!  A module-wide lock is being held during your
 * callback!  This method is mostly for the debug console to be able to list the
 * devices which are currently plugged in.
 */
void intel_hda_foreach(intel_hda_foreach_cbk cbk, void* ctx);

/**
 * Grab a reference and return a pointer to the Intel HDA device with the given
 * device ID.  intel_hda_release MUST be called to release the device reference
 * when finished.
 */
intel_hda_device_t* intel_hda_acquire(int dev_id);

/**
 * Release the reference for a device which was acquired using
 * intel_hda_acquire.
 */
void intel_hda_release(intel_hda_device_t* dev);

__END_CDECLS
