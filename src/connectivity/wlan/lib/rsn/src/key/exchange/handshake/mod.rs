// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

pub mod fourway;
pub mod group_key;

// This enum is primarily used for identifying the
// origin of different `wlan_rsn::Error`s.
#[derive(Debug)]
pub enum HandshakeMessageNumber {
    FourwayMessageNumber(fourway::MessageNumber),
}

impl From<fourway::MessageNumber> for HandshakeMessageNumber {
    fn from(e: fourway::MessageNumber) -> Self {
        HandshakeMessageNumber::FourwayMessageNumber(e)
    }
}
