// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fuchsia_zircon as zx;

pub fn is_peer_closed(e: &fidl::Error) -> bool {
    match e {
        fidl::Error::ServerResponseWrite(zx::Status::PEER_CLOSED)
        | fidl::Error::ServerRequestRead(zx::Status::PEER_CLOSED)
        | fidl::Error::ClientRead(zx::Status::PEER_CLOSED)
        | fidl::Error::ClientWrite(zx::Status::PEER_CLOSED) => true,
        _ => false,
    }
}
