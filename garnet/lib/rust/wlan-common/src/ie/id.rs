// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use super::Id;

// IEEE Std 802.11-2016, 9.4.2.1, Table 9-77
pub const SSID: Id = Id(0);
pub const SUPPORTED_RATES: Id = Id(1);
pub const DSSS_PARAM_SET: Id = Id(3);
pub const TIM: Id = Id(5);
pub const HT_CAPABILITIES: Id = Id(45);
pub const EXT_SUPPORTED_RATES: Id = Id(50);
pub const HT_OPERATION: Id = Id(61);
