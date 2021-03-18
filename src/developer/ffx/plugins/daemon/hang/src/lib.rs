// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Result, ffx_core::ffx_plugin, ffx_hang_args::HangCommand,
    fidl_fuchsia_developer_bridge as bridge,
};

#[ffx_plugin()]
pub async fn hang(daemon_proxy: bridge::DaemonProxy, _cmd: HangCommand) -> Result<()> {
    let _ = daemon_proxy.hang().await;
    Ok(())
}

#[cfg(test)]
mod test {
    use {
        super::*,
        fidl_fuchsia_developer_bridge::DaemonRequest,
        std::sync::atomic::{AtomicBool, Ordering},
    };

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_hang_with_no_text() {
        static HUNG: AtomicBool = AtomicBool::new(false);
        let proxy = setup_fake_daemon_proxy(|req| match req {
            DaemonRequest::Hang { .. } => {
                HUNG.store(true, Ordering::SeqCst);
            }
            _ => assert!(false),
        });
        assert!(hang(proxy, HangCommand {}).await.is_ok());
        assert!(HUNG.load(Ordering::SeqCst));
    }
}
