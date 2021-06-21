// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::anyhow,
    fidl_fuchsia_update::CommitStatusProviderProxy,
    fuchsia_zircon::{self as zx, AsHandleRef},
};

/// Whether the current system version is pending commit.
#[derive(Copy, Clone, Debug, PartialEq)]
pub enum CommitStatus {
    /// The current system version is pending commit.
    Pending,
    /// The current system version is already committed.
    Committed,
}

/// Queries the commit status using `provider`.
pub async fn query_commit_status(
    provider: &CommitStatusProviderProxy,
) -> Result<CommitStatus, anyhow::Error> {
    let event_pair = provider.is_current_system_committed().await.unwrap_or_else(|e| {
        // TODO(fxbug.dev/66760): remove this once we get critical components support in
        // v2. A FIDL error probably indicates the CommitStatusProvider crashed.
        // In order to prevent OTAs from being indefinitely blocked, we crash
        // here to force the system to reboot (since update checker is a
        // critical component). Ideally, we'd implement this in the component
        // serving CommitStatusProvider. However, since that component is v2, and v2
        // components do not have watchdog support, for now we'll make a "DIY
        // watchdog" here to ensure the system reboots. We can remove this once
        // there's a v2 equivalent to critical components.
        //
        // In practice, if the component serving CommitStatusProvider crashes, the
        // component manager should restart the crashed component once we try to
        // reconnect to CommitStatusProvider. We probably want to do a full
        // reboot when the component serving CommitStatusProvider crashes, but
        // the current behavior is acceptable for now.
        panic!("got error with is_current_system_committed(), crashing: {:#}", anyhow!(e));
    });
    match event_pair.wait_handle(zx::Signals::USER_0, zx::Time::INFINITE_PAST) {
        Ok(_) => Ok(CommitStatus::Committed),
        Err(zx::Status::TIMED_OUT) => Ok(CommitStatus::Pending),
        Err(status) => Err(anyhow!("unexpected status while asserting signal: {:?}", status)),
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        fidl::endpoints::create_proxy_and_stream,
        fidl_fuchsia_update::{CommitStatusProviderMarker, CommitStatusProviderRequest},
        fuchsia_async::{self as fasync, futures::StreamExt},
        fuchsia_zircon::{HandleBased, Peered},
    };

    // Verifies that query_commit_status returns the expected CommitStatus.
    #[fasync::run_singlethreaded(test)]
    async fn test_query_commit_status() {
        let (proxy, mut stream) = create_proxy_and_stream::<CommitStatusProviderMarker>().unwrap();
        let (p0, p1) = zx::EventPair::create().unwrap();

        let _fidl_server = fasync::Task::local(async move {
            while let Some(Ok(req)) = stream.next().await {
                let CommitStatusProviderRequest::IsCurrentSystemCommitted { responder } = req;
                let () = responder.send(p1.duplicate_handle(zx::Rights::BASIC).unwrap()).unwrap();
            }
        });

        // When no signals are asserted, we should report Pending.
        assert_eq!(query_commit_status(&proxy).await.unwrap(), CommitStatus::Pending);

        // When USER_0 is asserted, we should report Committed.
        let () = p0.signal_peer(zx::Signals::NONE, zx::Signals::USER_0).unwrap();
        assert_eq!(query_commit_status(&proxy).await.unwrap(), CommitStatus::Committed,);
    }

    // Verifies we panic when the FIDL call in query_commit_status fails.
    // TODO(fxbug.dev/66760): fold this into `test_query_commit_status` since it
    // shouldn't panic.
    #[fasync::run_singlethreaded(test)]
    #[should_panic]
    async fn test_query_commit_status_panic() {
        let (proxy, stream) = create_proxy_and_stream::<CommitStatusProviderMarker>().unwrap();
        drop(stream);
        let _ = query_commit_status(&proxy).await;
    }
}
