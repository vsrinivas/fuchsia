// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Implements fuchsia.net.icmp FIDL for creating simple sockets for sending and receiving ICMP
//! messages. This is useful for debugging the network stack within other applications.
//!
//! The entry point for all ICMP-related tasks is fuchsia.net.icmp.Provider.

pub mod provider;
