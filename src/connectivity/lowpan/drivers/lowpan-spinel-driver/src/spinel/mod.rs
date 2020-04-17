// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

pub mod device_client;
pub use device_client::*;

#[cfg(test)]
pub mod mock;
