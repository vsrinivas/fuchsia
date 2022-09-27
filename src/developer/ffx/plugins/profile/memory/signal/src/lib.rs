// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Implementation of the `signal` subcommand.

use {
    anyhow::Result, ffx_core::ffx_plugin, ffx_profile_memory_signal_args::SignalCommand,
    fidl_fuchsia_memory::DebuggerProxy,
};

/// Forwards the specified memory pressure level to the fuchsia.memory.Debugger FIDL interface.
#[ffx_plugin(
    "ffx_memory_signal",
    DebuggerProxy = "core/memory_monitor:expose:fuchsia.memory.Debugger"
)]
pub async fn signal(debugger_proxy: DebuggerProxy, cmd: SignalCommand) -> Result<()> {
    Ok(debugger_proxy.signal_memory_pressure(cmd.level)?)
}
