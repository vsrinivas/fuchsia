// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::fx_log_metadata_t;
use fidl_fuchsia_logger::MAX_DATAGRAM_LEN_BYTES;

pub const METADATA_SIZE: usize = std::mem::size_of::<fx_log_metadata_t>();
pub const MIN_PACKET_SIZE: usize = METADATA_SIZE + 1;

pub const MAX_DATAGRAM_LEN: usize = MAX_DATAGRAM_LEN_BYTES as _;
pub const MAX_TAGS: usize = 5;
pub const MAX_TAG_LEN: usize = 64;
