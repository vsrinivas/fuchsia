// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![deny(missing_docs)]

//! fidl_contrib contains libraries useful for interacting with FIDL, but not directly owned by the FIDL team.

pub mod protocol_connector;

pub use protocol_connector::ProtocolConnector;
