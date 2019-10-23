// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Implements fuchsia.net.icmp FIDL for creating simple sockets for sending and receiving ICMP
//! messages. This is useful for debugging the network stack within other applications.
//!
//! The entry point for all ICMP-related tasks is fuchsia.net.icmp.Provider.

pub mod echo;
pub mod provider;

// ICMP messages are buffered upon receival to allow circumvention of DoS attack vectors.
// Messages are taken from these buffers by calling EchoSocket's `watch` method, giving the client
// the ability to throttle consumption.
const RX_BUFFER_SIZE: usize = 256;
