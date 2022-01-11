// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Result, ffx_core::ffx_plugin, ffx_crash_args::CrashCommand,
    fidl_fuchsia_developer_bridge::TestingProxy,
};

#[ffx_plugin(TestingProxy = "daemon::protocol")]
pub async fn crash(testing_proxy: TestingProxy, _cmd: CrashCommand) -> Result<()> {
    let _ = testing_proxy.crash().await;
    Ok(())
}

#[cfg(test)]
mod test {
    use {
        super::*,
        fidl_fuchsia_developer_bridge::TestingRequest,
        std::sync::atomic::{AtomicBool, Ordering},
    };

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_crash_with_no_text() {
        // XXX(raggi): if we can bound the lifetime of the testing proxy setup as
        // desired by the test, then we could avoid the need for the static.
        static CRASHED: AtomicBool = AtomicBool::new(false);
        let proxy = setup_fake_testing_proxy(|req| match req {
            TestingRequest::Crash { .. } => {
                CRASHED.store(true, Ordering::SeqCst);
            }
            _ => assert!(false),
        });
        assert!(crash(proxy, CrashCommand {}).await.is_ok());
        assert!(CRASHED.load(Ordering::SeqCst));
    }
}
