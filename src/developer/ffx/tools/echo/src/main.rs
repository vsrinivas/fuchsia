// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
use ffx_tool_echo::EchoTool;
use fho::FfxMain;

#[fuchsia_async::run_singlethreaded]
async fn main() {
    EchoTool::execute_tool().await
}
