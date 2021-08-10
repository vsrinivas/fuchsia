// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

pub const TIM_MAX_BITMAP_LEN: usize = 251;
pub const SUPPORTED_RATES_MAX_LEN: usize = 8;
pub const EXTENDED_SUPPORTED_RATES_MAX_LEN: usize = 255;
pub const PREQ_MAX_TARGETS: usize = 20;
pub const PERR_MAX_DESTINATIONS: usize = 19;
pub const PERR_MAX_DESTINATION_SIZE: usize = 19;
pub const IE_PREFIX_LEN: usize = 2; // every IE contains 1 byte for ID and 1 byte for length
pub const IE_MAX_LEN: usize = 255; // does not include prefix

/// WFA WMM v1.2, 2.2.1 Table 3 and 2.2.2 Table 5
pub const WMM_OUI_TYPE: u8 = 2;
/// WFA WMM v1.2, 2.2.1 Table 3
pub const WMM_INFO_OUI_SUBTYPE: u8 = 0;
/// WFA WMM v1.2, 2.2.2 Table 5
pub const WMM_PARAM_OUI_SUBTYPE: u8 = 1;
