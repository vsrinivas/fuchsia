// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::update::CommitAction,
    anyhow::{anyhow, Context},
    fidl_fuchsia_hardware_power_statecontrol::AdminProxy as PowerStateControlProxy,
    fuchsia_async::{Task, TimeoutExt},
    fuchsia_syslog::fx_log_err,
    futures::prelude::*,
    std::time::Duration,
};

// The system-updater does not want to manage the policy of when to schedule a reboot.  As a
// failsafe against an initiator holding onto a controller and never scheduling a time to reboot,
// timeout the wait after an unreasonably long time.
const WAIT_TO_REBOOT_FAILSAFE_TIMEOUT: Duration = Duration::from_secs(7 * 24 * 60 * 60);

/// External controller that determines when the update attempt should reboot into the new system.
pub struct RebootController {
    task: Task<ControlRequest>,
}

pub enum ControlRequest {
    Unblock,
    Detach,
}

impl RebootController {
    /// Creates a RebootController that spawns the given future, using its output as the reboot
    /// control request. The provided future will be dropped when the returned RebootController is
    /// dropped.
    pub fn spawn<F>(fut: F) -> Self
    where
        F: Future<Output = ControlRequest> + Send + 'static,
    {
        Self { task: Task::spawn(fut) }
    }

    /// Creates a RebootController that is immediately ready to reboot.
    pub fn unblocked() -> Self {
        Self::spawn(future::ready(ControlRequest::Unblock))
    }

    /// Wait for the external controller to signal it is time for the reboot.
    pub(super) async fn wait_to_reboot(self) -> CommitAction {
        let on_timeout = || {
            fx_log_err!("RebootController failsafe triggered, force unblocking reboot");
            ControlRequest::Unblock
        };

        match self.task.on_timeout(WAIT_TO_REBOOT_FAILSAFE_TIMEOUT, on_timeout).await {
            ControlRequest::Unblock => CommitAction::Reboot,
            ControlRequest::Detach => CommitAction::RebootDeferred,
        }
    }
}

/// Reboots the system, logging errors instead of failing.
pub(super) async fn reboot(proxy: &PowerStateControlProxy) {
    if let Err(e) = async move {
        use fidl_fuchsia_hardware_power_statecontrol::RebootReason;
        proxy
            .reboot(RebootReason::SystemUpdate)
            .await
            .context("while performing reboot call")?
            .map_err(fuchsia_zircon::Status::from_raw)
            .context("reboot responded with")
    }
    .await
    {
        fx_log_err!("error initiating reboot: {:#}", anyhow!(e));
    }
}

#[cfg(test)]
mod tests {
    use {super::*, futures::task::Poll};
    #[allow(clippy::bool_assert_comparison)]
    #[test]
    fn wait_to_reboot_times_out() {
        let mut executor = fuchsia_async::TestExecutor::new_with_fake_time().unwrap();

        let mut wait = RebootController::spawn(future::pending()).wait_to_reboot().boxed();
        assert_eq!(executor.run_until_stalled(&mut wait), Poll::Pending);

        const ONE_DAY: Duration = Duration::from_secs(24 * 60 * 60);

        executor.set_fake_time(executor.now() + ONE_DAY.into());
        assert_eq!(executor.wake_expired_timers(), false);
        assert_eq!(executor.run_until_stalled(&mut wait), Poll::Pending);

        executor.set_fake_time(executor.now() + (7 * ONE_DAY).into());
        assert_eq!(executor.wake_expired_timers(), true);
        assert_eq!(executor.run_until_stalled(&mut wait), Poll::Ready(CommitAction::Reboot));
    }
}
