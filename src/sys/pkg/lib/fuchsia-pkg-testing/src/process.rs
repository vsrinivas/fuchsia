// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{anyhow, Context as _, Error},
    fidl::AsHandleRef as _,
    fuchsia_zircon as zx,
};

pub async fn wait_for_process_termination(proc: zx::Process) -> Result<(), Error> {
    let signals =
        fuchsia_async::OnSignals::new(&proc.as_handle_ref(), zx::Signals::PROCESS_TERMINATED)
            .await
            .context("waiting for tool to terminate")?;
    assert_eq!(signals, zx::Signals::PROCESS_TERMINATED);

    let ret = proc.info().context("getting tool process info")?.return_code;
    if ret != 0 {
        return Err(anyhow!("tool returned nonzero exit code {}", ret));
    }
    Ok(())
}
