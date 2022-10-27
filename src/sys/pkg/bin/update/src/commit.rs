// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{Context, Error},
    fidl_fuchsia_update::{CommitStatusProviderMarker, CommitStatusProviderProxy},
    fuchsia_async as fasync,
    fuchsia_component::client::connect_to_protocol,
    fuchsia_zircon as zx,
    futures::{future::FusedFuture, prelude::*},
    std::time::Duration,
};

const WARNING_DURATION: Duration = Duration::from_secs(30);

/// Connects to the FIDL service, waits for the commit, and prints updates to stdout.
pub async fn handle_wait_for_commit() -> Result<(), Error> {
    let proxy = connect_to_protocol::<CommitStatusProviderMarker>()
        .context("while connecting to fuchsia.update/CommitStatusProvider")?;
    handle_wait_for_commit_impl(&proxy, Printer).await
}

/// The set of events associated with the `wait-for-commit` path.
#[derive(Debug, PartialEq)]
enum CommitEvent {
    Begin,
    Warning,
    End,
}

/// An observer of `update wait-for-commit`.
trait CommitObserver {
    fn on_event(&self, event: CommitEvent);
}

/// A `CommitObserver` that forwards the events to stdout.
struct Printer;
impl CommitObserver for Printer {
    fn on_event(&self, event: CommitEvent) {
        let text = match event {
            CommitEvent::Begin => "Waiting for commit.",
            CommitEvent::Warning => {
                "It's been 30 seconds. Something is probably wrong. Consider \
                running `update revert` to fall back to the previous slot."
            }
            CommitEvent::End => "Committed!",
        };
        println!("{}", text);
    }
}

/// Waits for the system to commit (e.g. when the EventPair observes a signal).
async fn wait_for_commit(proxy: &CommitStatusProviderProxy) -> Result<(), Error> {
    let p = proxy.is_current_system_committed().await.context("while obtaining EventPair")?;
    fasync::OnSignals::new(&p, zx::Signals::USER_0)
        .await
        .context("while waiting for the commit")?;
    Ok(())
}

/// Waits for the commit and sends updates to the observer. This is abstracted from the regular
/// `handle_wait_for_commit` fn so we can test events without having to wait the `WARNING_DURATION`.
/// The [testability rubric](https://fuchsia.dev/fuchsia-src/concepts/testing/testability_rubric)
/// exempts logs from testing, but in this case we test them anyway because of the additional layer
/// of complexity that the warning timeout introduces.
async fn handle_wait_for_commit_impl(
    proxy: &CommitStatusProviderProxy,
    observer: impl CommitObserver,
) -> Result<(), Error> {
    let () = observer.on_event(CommitEvent::Begin);

    let commit_fut = wait_for_commit(proxy).fuse();
    futures::pin_mut!(commit_fut);
    let mut timer_fut = fasync::Timer::new(WARNING_DURATION).fuse();

    // Send a warning after the WARNING_DURATION.
    let () = futures::select! {
        commit_res = commit_fut => commit_res?,
        _ = timer_fut => observer.on_event(CommitEvent::Warning),
    };

    // If we timed out on WARNING_DURATION, try again.
    if !commit_fut.is_terminated() {
        let () = commit_fut.await.context("while calling wait_for_commit second")?;
    }

    let () = observer.on_event(CommitEvent::End);
    Ok(())
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        fidl_fuchsia_update::CommitStatusProviderRequest,
        fuchsia_zircon::{DurationNum, EventPair, HandleBased, Peered},
        futures::{pin_mut, task::Poll},
        parking_lot::Mutex,
    };

    struct TestObserver {
        events: Mutex<Vec<CommitEvent>>,
    }
    impl TestObserver {
        fn new() -> Self {
            Self { events: Mutex::new(vec![]) }
        }
        fn assert_events(&self, expected_events: &[CommitEvent]) {
            assert_eq!(self.events.lock().as_slice(), expected_events);
        }
    }
    impl CommitObserver for &TestObserver {
        fn on_event(&self, event: CommitEvent) {
            self.events.lock().push(event);
        }
    }

    #[test]
    fn test_wait_for_commit() {
        let mut executor = fasync::TestExecutor::new_with_fake_time().unwrap();

        let (proxy, mut stream) =
            fidl::endpoints::create_proxy_and_stream::<CommitStatusProviderMarker>().unwrap();
        let (p, p_stream) = EventPair::create().unwrap();
        fasync::Task::spawn(async move {
            while let Some(req) = stream.try_next().await.unwrap() {
                let CommitStatusProviderRequest::IsCurrentSystemCommitted { responder } = req;
                let pair = p_stream.duplicate_handle(zx::Rights::BASIC).unwrap();
                let () = responder.send(pair).unwrap();
            }
        })
        .detach();

        let observer = TestObserver::new();

        let fut = handle_wait_for_commit_impl(&proxy, &observer);
        pin_mut!(fut);

        // Begin the `wait_for_commit`.
        match executor.run_until_stalled(&mut fut) {
            Poll::Ready(res) => panic!("future unexpectedly completed with: {:?}", res),
            Poll::Pending => (),
        };
        observer.assert_events(&[CommitEvent::Begin]);

        // We should observe no new events when both the system is not committed and we are within
        // the warning duration.
        executor
            .set_fake_time(fasync::Time::after((WARNING_DURATION - Duration::from_secs(1)).into()));
        assert!(!executor.wake_expired_timers());
        match executor.run_until_stalled(&mut fut) {
            Poll::Ready(res) => panic!("future unexpectedly completed with: {:?}", res),
            Poll::Pending => (),
        };
        observer.assert_events(&[CommitEvent::Begin]);

        // Once we hit the warning duration, we should get a warning event.
        executor.set_fake_time(fasync::Time::after(1.seconds()));
        assert!(executor.wake_expired_timers());
        match executor.run_until_stalled(&mut fut) {
            Poll::Ready(res) => panic!("future unexpectedly completed with: {:?}", res),
            Poll::Pending => (),
        };
        observer.assert_events(&[CommitEvent::Begin, CommitEvent::Warning]);

        // Once we get the commit signal, the future should complete.
        let () = p.signal_peer(zx::Signals::NONE, zx::Signals::USER_0).unwrap();
        match executor.run_until_stalled(&mut fut) {
            Poll::Ready(res) => res.unwrap(),
            Poll::Pending => panic!("future unexpectedly pending"),
        };
        observer.assert_events(&[CommitEvent::Begin, CommitEvent::Warning, CommitEvent::End]);
    }
}
