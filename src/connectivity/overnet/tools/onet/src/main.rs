// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#[fuchsia::main]
async fn main() -> Result<(), anyhow::Error> {
    onet_tool::run_onet(hoist::init_hoist()?, argh::from_env()).await
}
