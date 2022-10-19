// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
// Keep all consts and type defs for completeness.
#![allow(dead_code)]

use {
    bitflags::bitflags,
    zerocopy::{AsBytes, FromBytes, LittleEndian, U16, U32, U64},
};

pub type LE16 = U16<LittleEndian>;
pub type LE32 = U32<LittleEndian>;
pub type LE64 = U64<LittleEndian>;

//
// 5.5.3 Feature bits
//
// VIRTIO_BALLOON_F_MUST_TELL_HOST (0) Host has to be told before pages from the balloon are
// used.
// VIRTIO_BALLOON_F_STATS_VQ (1) A virtqueue for reporting guest memory statistics is present.
// VIRTIO_BALLOON_F_DEFLATE_ON_OOM (2) Deflate balloon on guest out of memory condition.
// VIRTIO_BALLOON_F_FREE_PAGE_HINT(3) The device has support for free page hinting. A virtqueue
// for providing hints as to what memory is currently free is present. Configuration field free_page_hint_-
// cmd_id is valid.
// VIRTIO_BALLOON_F_PAGE_POISON(4) A hint to the device, that the driver will immediately write poi-
// son_val to pages after deflating them. Configuration field poison_val is valid.
// VIRTIO_BALLOON_F_PAGE_REPORTING(5) The device has support for free page reporting. A virtqueue
// for reporting free guest memory is present.
bitflags! {
    #[derive(Default)]
    pub struct VirtioBalloonFeatureFlags: u32 {
        const VIRTIO_BALLOON_F_MUST_TELL_HOST    = 0b00000001;
        const VIRTIO_BALLOON_F_STATS_VQ          = 0b00000010;
        const VIRTIO_BALLOON_F_DEFLATE_ON_OOM    = 0b00000100;
        const VIRTIO_BALLOON_F_FREE_PAGE_HINT    = 0b00001000;
        const VIRTIO_BALLOON_F_PAGE_POISON       = 0b00010000;
        const VIRTIO_BALLOON_F_PAGE_REPORTING    = 0b00100000;
    }
}
//
// 5.5.2 Virtqueues
//
pub const INFLATEQ: u16 = 0;
pub const DEFLATEQ: u16 = 1;
pub const STATSQ: u16 = 2;
//
// Linux virt queue negotiation increments queue index only for supported queue
// In the case of the balloon we have page hinting queue using index 3, but we
// don't use this queue. We use the following free page reporting queue which
// has index 4 in the spec. But since we don't advertise free page hinting
// support Linux will initialise free page reporting queue as number 3. See
// https://elixir.bootlin.com/linux/v6.0/source/drivers/virtio/virtio_balloon.c#L527
// and
// https://elixir.bootlin.com/linux/v6.0/source/drivers/virtio/virtio_pci_common.c#L376
// Hence the reason to use 3 here instead of what is defined in the spec
// Alternative would be to advertise and don't use free page hinting
//
pub const REPORTINGVQ: u16 = 3;

// 5.5.6 Device Operation
// To supply memory to the balloon (aka. inflate):
// (a) The driver constructs an array of addresses of unused memory pages. These addresses are
// divided by 4096 and the descriptor describing the resulting 32-bit array is added to the inflateq
//
// 4096 is historical, and independent of the guest page size.
pub const PAGE_SIZE: usize = 4096;

#[derive(Debug, Copy, Clone, AsBytes, FromBytes)]
#[repr(C, packed)]
pub struct VirtioBalloonMemStat {
    pub tag: LE16,
    pub val: LE64,
}
