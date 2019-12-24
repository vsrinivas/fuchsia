// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

mod check;
mod mark;

pub use check::check_system_health;
pub use mark::set_active_configuration_healthy;
