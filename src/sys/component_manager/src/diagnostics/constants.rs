// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use lazy_static::lazy_static;
use std::time::Duration;

pub const CPU_SAMPLE_PERIOD: Duration = Duration::from_secs(60);
pub const COMPONENT_CPU_MAX_SAMPLES: usize = 60;
pub const MAX_DEAD_TASKS: usize = 90;

lazy_static! {
    pub static ref MEASUREMENT_EPSILON: Duration = CPU_SAMPLE_PERIOD / 6;
}
