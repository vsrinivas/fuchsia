// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::fmt::Debug;

/// Denotes the type of task spawned from the task handler.
#[derive(Debug, Hash, Eq, PartialEq, Clone)]
#[allow(dead_code)]
pub enum TargetTaskType {
    HostPipe,
    MdnsMonitor,
    ProactiveLog,
}
