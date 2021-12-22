// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fuchsia_component::server::ServiceFs;
use futures::stream::StreamExt;

#[fuchsia::component]
async fn main() {
    let mut fs = ServiceFs::new();
    fs.take_and_serve_directory_handle().expect("serve svc");
    fs.collect::<()>().await;
}
