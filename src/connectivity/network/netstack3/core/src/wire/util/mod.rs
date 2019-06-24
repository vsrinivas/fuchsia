// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

pub(crate) mod checksum;
pub(crate) mod records;

use byteorder::NetworkEndian;

pub(crate) type U16 = zerocopy::U16<NetworkEndian>;
pub(crate) type U32 = zerocopy::U32<NetworkEndian>;
