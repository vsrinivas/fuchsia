// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::fuzzctl::FuzzCtl,
    anyhow::{Context as _, Result},
    fidl_fuchsia_fuzzer as fuzz,
    fuchsia_component::client::connect_to_protocol,
    fuchsia_fuzzctl::{StdioSink, Writer},
    std::path::PathBuf,
};

mod args;
mod fuzzctl;

#[fuchsia::main(logging = true)]
async fn main() -> Result<()> {
    let proxy = connect_to_protocol::<fuzz::ManagerMarker>()
        .context("failed to connect to fuchsia.fuzzer.Manager")?;
    let output_dir = PathBuf::from("/tmp/fuzz_ctl");
    let mut writer = Writer::new(StdioSink { is_tty: false });
    writer.use_colors(false);
    let fuzz_ctl = FuzzCtl::new(proxy, output_dir, &writer);
    let args: Vec<String> = std::env::args().skip(1).collect();
    fuzz_ctl.run(&args).await
}
