// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use bitflags::bitflags;

bitflags! {
    #[derive(Default)]
    pub struct PathSpace: u8 {
        const LOCAL = 0b0001;
        const WORLD = 0b0010;
        const DIFFERENCE = 0b0100;
        const CLIPPING = 0b1000;
    }
}
