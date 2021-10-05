// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::time::Duration;

pub const CPU_SAMPLE_PERIOD: Duration = Duration::from_secs(60);
pub const COMPONENT_CPU_MAX_SAMPLES: usize = 60;
