// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{anyhow, Result},
    fuchsia_async::unblock,
    std::process::Command,
};

#[ffx_core::ffx_plugin()]
pub async fn symbolize(cmd: ffx_debug_symbolize_args::SymbolizeCommand) -> Result<i32> {
    if let Err(e) = symbol_index::ensure_symbol_index_registered().await {
        eprintln!("ensure_symbol_index_registered failed, error was: {:#?}", e);
    }

    let symbolizer_path = ffx_config::get_sdk().await?.get_host_tool("symbolizer")?;
    let mut args = cmd.symbolizer_args;
    if cmd.auth {
        args.push("--auth".to_owned());
    }

    let mut cmd = Command::new(symbolizer_path).args(args).spawn()?;

    if let Some(exit_code) = unblock(move || cmd.wait()).await?.code() {
        Ok(exit_code)
    } else {
        Err(anyhow!("symbolizer terminated by signal"))
    }
}
