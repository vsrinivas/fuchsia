// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{anyhow, Context as _, Error},
    fidl_fuchsia_update::{
        CheckOptions, CommitStatusProviderMarker, CommitStatusProviderProxy, Initiator,
        ManagerMarker, ManagerProxy, MonitorMarker, MonitorRequest, MonitorRequestStream,
    },
    fidl_fuchsia_update_channelcontrol::{ChannelControlMarker, ChannelControlProxy},
    fidl_fuchsia_update_ext::State,
    fidl_fuchsia_update_installer::{InstallerMarker, RebootControllerMarker},
    fidl_fuchsia_update_installer_ext::{self as installer, start_update, Options, StateId},
    fuchsia_async as fasync,
    fuchsia_component::client::connect_to_service,
    fuchsia_url::pkg_url::PkgUrl,
    fuchsia_zircon as zx,
    futures::{future::FusedFuture, prelude::*},
    std::time::Duration,
};

mod args;

const WARNING_DURATION: Duration = Duration::from_secs(30);

fn print_state(state: &State) {
    println!("State: {:?}", state);
}

async fn monitor_state(mut stream: MonitorRequestStream) -> Result<(), Error> {
    while let Some(event) = stream.try_next().await? {
        match event {
            MonitorRequest::OnState { state, responder } => {
                responder.send()?;

                let state = State::from(state);

                // Exit if we encounter an error during an update.
                if state.is_error() {
                    anyhow::bail!("Update failed: {:?}", state)
                } else {
                    print_state(&state);
                }
            }
        }
    }
    Ok(())
}

async fn handle_channel_control_cmd(
    cmd: args::channel::Command,
    channel_control: ChannelControlProxy,
) -> Result<(), Error> {
    match cmd {
        args::channel::Command::Get(_) => {
            let channel = channel_control.get_current().await?;
            println!("current channel: {}", channel);
        }
        args::channel::Command::Target(_) => {
            let channel = channel_control.get_target().await?;
            println!("target channel: {}", channel);
        }
        args::channel::Command::Set(args::channel::Set { channel }) => {
            channel_control.set_target(&channel).await?;
        }
        args::channel::Command::List(_) => {
            let channels = channel_control.get_target_list().await?;
            if channels.is_empty() {
                println!("known channels list is empty.");
            } else {
                println!("known channels:");
                for channel in channels {
                    println!("{}", channel);
                }
            }
        }
    }
    Ok(())
}

async fn handle_check_now_cmd(
    cmd: args::CheckNow,
    update_manager: ManagerProxy,
) -> Result<(), Error> {
    let args::CheckNow { service_initiated, monitor } = cmd;
    let options = CheckOptions {
        initiator: Some(if service_initiated { Initiator::Service } else { Initiator::User }),
        allow_attaching_to_existing_update_check: Some(true),
        ..CheckOptions::EMPTY
    };
    let (monitor_client, monitor_server) = if monitor {
        let (client_end, request_stream) =
            fidl::endpoints::create_request_stream::<MonitorMarker>()?;
        (Some(client_end), Some(request_stream))
    } else {
        (None, None)
    };
    if let Err(e) = update_manager.check_now(options, monitor_client).await? {
        anyhow::bail!("Update check failed to start: {:?}", e);
    }
    println!("Checking for an update.");
    if let Some(monitor_server) = monitor_server {
        monitor_state(monitor_server).await?;
    }
    Ok(())
}

async fn force_install(
    update_pkg_url: String,
    reboot: bool,
    service_initiated: bool,
) -> Result<(), Error> {
    let pkgurl = PkgUrl::parse(&update_pkg_url).context("parsing update package url")?;

    let options = Options {
        initiator: if service_initiated {
            installer::Initiator::Service
        } else {
            installer::Initiator::User
        },
        should_write_recovery: true,
        allow_attach_to_existing_attempt: true,
    };

    let proxy = connect_to_service::<InstallerMarker>()
        .context("connecting to fuchsia.update.installer")?;

    let (reboot_controller, reboot_controller_server_end) =
        fidl::endpoints::create_proxy::<RebootControllerMarker>()
            .context("creating reboot controller")?;

    let mut update_attempt =
        start_update(&pkgurl, options, &proxy, Some(reboot_controller_server_end))
            .await
            .context("starting update")?;

    println!("Installing an update.");
    if !reboot {
        reboot_controller.detach().context("notify installer do not reboot")?;
    }
    while let Some(state) = update_attempt.try_next().await.context("getting next state")? {
        println!("State: {:?}", state);
        if state.id() == StateId::WaitToReboot {
            if reboot {
                return Ok(());
            }
        } else if state.is_success() {
            return Ok(());
        } else if state.is_failure() {
            anyhow::bail!("Encountered failure state");
        }
    }

    Err(anyhow!("Installation ended unexpectedly"))
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
            // TODO(fxbug.dev/64590) update warning message to be more helpful.
            CommitEvent::Warning => "It's been 30 seconds. Something is probably wrong.",
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

    let commit_fut = wait_for_commit(&proxy).fuse();
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

/// Waits for the commit and prints updates to stdout.
async fn handle_wait_for_commit(proxy: &CommitStatusProviderProxy) -> Result<(), Error> {
    handle_wait_for_commit_impl(&proxy, Printer).await
}

async fn handle_cmd(cmd: args::Command) -> Result<(), Error> {
    match cmd {
        args::Command::Channel(args::Channel { cmd }) => {
            let channel_control = connect_to_service::<ChannelControlMarker>()
                .context("Failed to connect to channel control service")?;

            handle_channel_control_cmd(cmd, channel_control).await?;
        }
        args::Command::CheckNow(check_now) => {
            let update_manager = connect_to_service::<ManagerMarker>()
                .context("Failed to connect to update manager")?;
            handle_check_now_cmd(check_now, update_manager).await?;
        }
        args::Command::ForceInstall(args) => {
            force_install(args.update_pkg_url, args.reboot, args.service_initiated).await?;
        }
        args::Command::WaitForCommit(_) => {
            let proxy = connect_to_service::<CommitStatusProviderMarker>()
                .context("while connecting to fuchsia.update/CommitStatusProvider")?;
            handle_wait_for_commit(&proxy).await?;
        }
    }
    Ok(())
}

pub fn main() -> Result<(), Error> {
    let mut executor = fasync::Executor::new()?;
    let args::Update { cmd } = argh::from_env();
    executor.run_singlethreaded(handle_cmd(cmd))
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        fidl::endpoints::create_proxy_and_stream,
        fidl_fuchsia_update::CommitStatusProviderRequest,
        fidl_fuchsia_update_channelcontrol::ChannelControlRequest,
        fuchsia_zircon::{DurationNum, EventPair, HandleBased, Peered},
        futures::{pin_mut, task::Poll},
        matches::assert_matches,
        parking_lot::Mutex,
    };

    async fn perform_channel_control_test<V>(argument: args::channel::Command, verifier: V)
    where
        V: Fn(ChannelControlRequest),
    {
        let (proxy, mut stream) = create_proxy_and_stream::<ChannelControlMarker>().unwrap();
        let fut = async move {
            assert_matches!(handle_channel_control_cmd(argument, proxy).await, Ok(()));
        };
        let stream_fut = async move {
            let result = stream.next().await.unwrap();
            match result {
                Ok(cmd) => verifier(cmd),
                err => panic!("Err in request handler: {:?}", err),
            }
        };
        future::join(fut, stream_fut).await;
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_channel_get() {
        perform_channel_control_test(args::channel::Command::Get(args::channel::Get {}), |cmd| {
            match cmd {
                ChannelControlRequest::GetCurrent { responder } => {
                    responder.send("channel").unwrap();
                }
                request => panic!("Unexpected request: {:?}", request),
            }
        })
        .await;
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_channel_target() {
        perform_channel_control_test(
            args::channel::Command::Target(args::channel::Target {}),
            |cmd| match cmd {
                ChannelControlRequest::GetTarget { responder } => {
                    responder.send("target-channel").unwrap();
                }
                request => panic!("Unexpected request: {:?}", request),
            },
        )
        .await;
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_channel_set() {
        perform_channel_control_test(
            args::channel::Command::Set(args::channel::Set { channel: "new-channel".to_string() }),
            |cmd| match cmd {
                ChannelControlRequest::SetTarget { channel, responder } => {
                    assert_eq!(channel, "new-channel");
                    responder.send().unwrap();
                }
                request => panic!("Unexpected request: {:?}", request),
            },
        )
        .await;
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_channel_list() {
        perform_channel_control_test(args::channel::Command::List(args::channel::List {}), |cmd| {
            match cmd {
                ChannelControlRequest::GetTargetList { responder } => {
                    responder.send(&mut vec!["some-channel", "other-channel"].into_iter()).unwrap();
                }
                request => panic!("Unexpected request: {:?}", request),
            }
        })
        .await;
    }

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
    fn wait_for_commit() {
        let mut executor = fasync::Executor::new_with_fake_time().unwrap();

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
