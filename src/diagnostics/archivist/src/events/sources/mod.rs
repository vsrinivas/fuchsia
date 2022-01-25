// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

pub use crate::events::sources::{
    legacy::*, log_connector::*, static_event_stream::*, unattributed_log_sink::*,
};

mod legacy;
mod log_connector;
mod static_event_stream;
mod unattributed_log_sink;
