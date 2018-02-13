// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

pub mod scan;

pub type Bssid = [u8; 6];
pub type Ssid = Vec<u8>;
pub type ChannelNumber = u8;
