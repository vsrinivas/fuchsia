// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#[fuchsia_async::run_singlethreaded]
async fn main() -> Result<(), anyhow::Error> {
    onet_lib::run_onet(argh::from_env()).await
}
