// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fuchsia_async as fasync;

#[fasync::run_singlethreaded]
/// Simple program which effectively never returns.
async fn main() {
    futures::future::pending::<()>().await;
}
