// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::Error;
use server::{HttpServer, ServerController};

#[fuchsia::main(logging_tags = ["file-server"])]
async fn main() -> Result<(), Error> {
    let http_server = HttpServer {};
    // address: http://127.0.0.1:81
    http_server.start(81).await;
    Ok(())
}
