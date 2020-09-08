// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fidl_fuchsia_location_position::{Position, PositionExtras};
use serde::Serialize;

#[derive(Serialize)]
#[serde(remote = "Position")]
struct PositionDef {
    pub latitude: f64,
    pub longitude: f64,
    #[serde(with = "PositionExtrasDef")]
    pub extras: PositionExtras,
}

// TODO(fxb/59274): Do not use Serde remote for FIDL tables.
#[derive(Serialize)]
#[serde(remote = "PositionExtras")]
struct PositionExtrasDef {
    pub accuracy_meters: Option<f64>,
    pub altitude_meters: Option<f64>,
}

#[derive(Serialize)]
pub struct PositionSerializer(#[serde(with = "PositionDef")] pub Position);
