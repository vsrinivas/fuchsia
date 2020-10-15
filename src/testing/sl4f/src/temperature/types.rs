// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use serde::Deserialize;

#[derive(Deserialize, Debug)]
pub struct TemperatureRequest {
    pub device_path: String,
}
