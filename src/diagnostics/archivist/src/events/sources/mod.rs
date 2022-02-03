// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

pub use crate::events::sources::{
    event_source::*, legacy::*, log_connector::*, unattributed_log_sink::*,
};

mod event_source;
mod legacy;
mod log_connector;
mod unattributed_log_sink;
