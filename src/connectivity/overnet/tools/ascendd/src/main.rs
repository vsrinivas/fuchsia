// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {anyhow::Error, ascendd::Ascendd};

#[fuchsia::component]
async fn main() -> Result<(), Error> {
    hoist::disable_autoconnect();
    Ascendd::new(argh::from_env(), Box::new(blocking::Unblock::new(std::io::stderr()))).await?.await
}
