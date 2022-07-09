// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// TODO(fxb/104019): Consider enabling globally.
#![deny(unused_crate_dependencies)]

mod quic_link;
mod run;

pub use run::run_udp;

// Exposed for testing purposes only.
pub use quic_link::{new_quic_link, QuicReceiver, QuicSender};
