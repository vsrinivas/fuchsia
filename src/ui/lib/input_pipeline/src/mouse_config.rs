// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Provides types related to mouse configuration events. Such
//! events are generated and consumed entirely within the input
//! pipeline library.

#[derive(Clone, Debug, PartialEq)]
pub enum MouseConfigEvent {
    ToggleImmersiveMode,
}
