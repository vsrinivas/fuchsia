// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Expectations for the Bluetooth Host Driver (bt-host)

use super::Predicate;
use crate::{over, types::HostInfo};

pub fn name(expected_name: &str) -> Predicate<HostInfo> {
    let name = Some(expected_name.to_string());
    over!(HostInfo: local_name, Predicate::equal(name))
}
pub fn discovering(discovering: bool) -> Predicate<HostInfo> {
    over!(HostInfo: discovering, Predicate::equal(discovering))
}
pub fn discoverable(discoverable: bool) -> Predicate<HostInfo> {
    over!(HostInfo: discoverable, Predicate::equal(discoverable))
}
