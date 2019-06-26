// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::organization::Oui;

pub const OUI: Oui = Oui::DOT11;

pub trait Factory {
    type Suite;

    fn new(oui: Oui, suite_type: u8) -> Self::Suite;
}
