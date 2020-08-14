// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::constants::*,
    crate::daemon_manager::{DaemonManager, DefaultDaemonManager},
    anyhow::{Error, Result},
    async_std::future::timeout,
    ffx_core::ffx_plugin,
    ffx_doctor_args::DoctorCommand,
    fidl::endpoints::create_proxy,
    fidl_fuchsia_developer_bridge::DaemonProxy,
    fidl_fuchsia_developer_remotecontrol::RemoteControlMarker,
    std::io::{stdout, Write},
    std::time::Duration,
};

mod constants;
mod daemon_manager;

fn format_err(e: Error) -> String {
    format!("{}\n\t{:?}", FAILED_WITH_ERROR, e)
}

fn print_status_line(writer: &mut impl Write, s: &str) {
    write!(writer, "{}", s).unwrap();
    writer.flush().unwrap();
}

#[ffx_plugin()]
pub async fn doctor_cmd(cmd: DoctorCommand) -> Result<()> {
    let mut writer = Box::new(stdout());
    let daemon_manager = DefaultDaemonManager {};
    let delay = Duration::from_millis(cmd.retry_delay);
    doctor(&mut writer, &daemon_manager, cmd.retry_count, delay).await
}

async fn doctor<W: Write>(
    writer: &mut W,
    daemon_manager: &impl DaemonManager,
    retry_count: usize,
    retry_delay: Duration,
) -> Result<()> {
    let mut proxy_opt: Option<DaemonProxy> = None;
    for i in 0..retry_count {
        proxy_opt = None;
        if i > 0 {
            daemon_manager.kill_all()?;
            writeln!(writer, "\n\nAttempt {} of {}", i + 1, retry_count).unwrap();
        }

        print_status_line(writer, DAEMON_RUNNING_CHECK);
        if !daemon_manager.is_running() {
            writeln!(writer, "{}", NONE_RUNNING).unwrap();
            print_status_line(writer, KILLING_ZOMBIE_DAEMONS);

            if daemon_manager.kill_all()? {
                writeln!(writer, "{}", ZOMBIE_KILLED).unwrap();
            } else {
                writeln!(writer, "{}", NONE_RUNNING).unwrap();
            }

            print_status_line(writer, SPAWNING_DAEMON);
            daemon_manager.spawn().await?;
            writeln!(writer, "{}", SUCCESS).unwrap();
        } else {
            writeln!(writer, "{}", FOUND).unwrap();
        }

        print_status_line(writer, CONNECTING_TO_DAEMON);
        match timeout(retry_delay, daemon_manager.find_and_connect()).await {
            Ok(Ok(p)) => {
                proxy_opt = Some(p);
            }

            Ok(Err(e)) => {
                writeln!(writer, "{}", format_err(e.into())).unwrap();
                continue;
            }
            Err(_) => {
                writeln!(writer, "{}", FAILED_TIMEOUT).unwrap();
                continue;
            }
        }

        writeln!(writer, "{}", SUCCESS).unwrap();
        print_status_line(writer, COMMUNICATING_WITH_DAEMON);
        match timeout(retry_delay, proxy_opt.as_ref().unwrap().echo_string("test")).await {
            Err(_) => {
                writeln!(writer, "{}", FAILED_TIMEOUT).unwrap();
                proxy_opt = None;
                continue;
            }
            Ok(Err(e)) => {
                writeln!(writer, "{}", format_err(e.into())).unwrap();
                proxy_opt = None;
                continue;
            }
            Ok(_) => {
                writeln!(writer, "{}", SUCCESS).unwrap();
            }
        }
        break;
    }

    if proxy_opt.is_none() {
        writeln!(writer, "{}", DAEMON_CHECKS_FAILED).unwrap();
        writeln!(writer, "Bug link: {}", BUG_URL).unwrap();
        return Ok(());
    }

    let daemon = proxy_opt.take().unwrap();
    let mut rcs_successful = false;

    for i in 0..retry_count {
        if i > 0 {
            writeln!(writer, "\n\nAttempt {} of {}", i + 1, retry_count).unwrap();
        }
        print_status_line(writer, LISTING_TARGETS);
        let targets = match timeout(retry_delay, daemon.list_targets("")).await {
            Err(_) => {
                writeln!(writer, "{}", FAILED_TIMEOUT).unwrap();
                continue;
            }
            Ok(Err(e)) => {
                writeln!(writer, "{}", format_err(e.into())).unwrap();
                continue;
            }
            Ok(t) => {
                writeln!(writer, "{}", SUCCESS).unwrap();
                t.unwrap()
            }
        };

        if targets.len() == 0 {
            if i == retry_count - 1 {
                writeln!(writer, "{}", NO_TARGETS_FOUND_EXTENDED).unwrap();
                writeln!(writer, "Bug link: {}", BUG_URL).unwrap();
                return Ok(());
            } else {
                writeln!(writer, "{}", NO_TARGETS_FOUND_SHORT).unwrap();
                continue;
            }
        }

        // TODO(jwing): 2 things:
        // 1) add a mechanism for choosing which target to connect to here
        // 2) SSH into the device and kill Overnet+RCS if anything below this fails
        print_status_line(writer, CONNECTING_TO_RCS);
        let target = targets.get(0).unwrap();
        let (remote_proxy, remote_server_end) = create_proxy::<RemoteControlMarker>()?;
        match timeout(
            retry_delay,
            daemon.get_remote_control(&target.nodename.as_ref().unwrap(), remote_server_end),
        )
        .await
        {
            Err(_) => {
                writeln!(writer, "{}", FAILED_TIMEOUT).unwrap();
                continue;
            }
            Ok(Err(e)) => {
                writeln!(writer, "{}", format_err(e.into())).unwrap();
                continue;
            }
            Ok(Ok(_)) => {
                writeln!(writer, "{}", SUCCESS).unwrap();
            }
        };

        print_status_line(writer, COMMUNICATING_WITH_RCS);
        match timeout(retry_delay, remote_proxy.identify_host()).await {
            Err(_) => {
                writeln!(writer, "{}", FAILED_TIMEOUT).unwrap();
                continue;
            }
            Ok(Err(e)) => {
                writeln!(writer, "{}", format_err(e.into())).unwrap();
                continue;
            }
            Ok(Ok(_)) => {
                writeln!(writer, "{}", SUCCESS).unwrap();
                rcs_successful = true;
                break;
            }
        };
    }

    if rcs_successful {
        writeln!(writer, "\n\n{}", ALL_CHECKS_PASSED).unwrap();
    } else {
        writeln!(writer, "\n\n{}", RCS_TERMINAL_FAILURE).unwrap();
        writeln!(writer, "Bug link: {}", BUG_URL).unwrap();
    }
    Ok(())
}

#[cfg(test)]
mod test {
    use {
        super::*,
        async_std::task,
        async_trait::async_trait,
        fidl::endpoints::{spawn_local_stream_handler, Request, ServerEnd, ServiceMarker},
        fidl_fuchsia_developer_bridge::{
            DaemonRequest, RemoteControlState, Target, TargetState, TargetType,
        },
        fidl_fuchsia_developer_remotecontrol::{
            IdentifyHostResponse, RemoteControlMarker, RemoteControlRequest,
        },
        fuchsia_async as fasync,
        futures::{Future, FutureExt, TryFutureExt, TryStreamExt},
        std::io::BufWriter,
        std::ops::Add,
        std::sync::{Arc, Mutex},
    };

    const NODENAME: &str = "fake-nodename";
    const DEFAULT_RETRY_DELAY: Duration = Duration::from_millis(2000);

    struct FakeStateManager {
        kill_results: Vec<Result<bool>>,
        daemons_running_results: Vec<bool>,
        spawn_results: Vec<Result<()>>,
        find_and_connect_results: Vec<Result<DaemonProxy>>,
    }

    struct FakeDaemonManager {
        state_manager: Arc<Mutex<FakeStateManager>>,
    }

    impl FakeDaemonManager {
        fn new(
            daemons_running_results: Vec<bool>,
            kill_results: Vec<Result<bool>>,
            spawn_results: Vec<Result<()>>,
            find_and_connect_results: Vec<Result<DaemonProxy>>,
        ) -> Self {
            return FakeDaemonManager {
                state_manager: Arc::new(Mutex::new(FakeStateManager {
                    kill_results,
                    daemons_running_results,
                    spawn_results,
                    find_and_connect_results,
                })),
            };
        }

        fn assert_no_leftover_calls(&self) {
            let state = self.state_manager.lock().unwrap();
            assert!(
                state.kill_results.is_empty(),
                format!("too few calls to kill_all. remaining entries: {:?}", state.kill_results)
            );
            assert!(
                state.daemons_running_results.is_empty(),
                format!(
                    "too few calls to is_running. remaining entries: {:?}",
                    state.daemons_running_results
                )
            );
            assert!(
                state.spawn_results.is_empty(),
                format!("too few calls to spawn. remaining entries: {:?}", state.spawn_results)
            );
            assert!(
                state.find_and_connect_results.is_empty(),
                format!(
                    "too few calls to find_and_connect. remaining entries: {:?}",
                    state.find_and_connect_results
                )
            );
        }
    }

    #[async_trait]
    impl DaemonManager for FakeDaemonManager {
        fn kill_all(&self) -> Result<bool> {
            let mut state = self.state_manager.lock().unwrap();
            assert!(!state.kill_results.is_empty(), "too many calls to kill_all");
            state.kill_results.remove(0)
        }

        fn is_running(&self) -> bool {
            let mut state = self.state_manager.lock().unwrap();
            assert!(!state.daemons_running_results.is_empty(), "too many calls to is_running");
            state.daemons_running_results.remove(0)
        }

        async fn spawn(&self) -> Result<()> {
            let mut state = self.state_manager.lock().unwrap();
            assert!(!state.spawn_results.is_empty(), "too many calls to spawn");
            state.spawn_results.remove(0)
        }

        async fn find_and_connect(&self) -> Result<DaemonProxy> {
            let mut state = self.state_manager.lock().unwrap();
            assert!(
                !state.find_and_connect_results.is_empty(),
                "too many calls to find_and_connect"
            );
            state.find_and_connect_results.remove(0)
        }
    }

    fn print_full_output(output: &str) {
        println!("BEGIN DOCTOR OUTPUT");
        println!("{}", &output);
        println!("END DOCTOR OUTPUT");
    }

    fn serve_stream<T, F, Fut>(stream: T::RequestStream, mut f: F)
    where
        T: ServiceMarker,
        F: FnMut(Request<T>) -> Fut + 'static + std::marker::Send,
        Fut: Future<Output = ()> + 'static + std::marker::Send,
    {
        fasync::Task::spawn(
            stream
                .try_for_each(move |r| f(r).map(Ok))
                .unwrap_or_else(|e| panic!(format!("failed to handle request: {:?}", e))),
        )
        .detach();
    }

    fn setup_responsive_daemon_server() -> DaemonProxy {
        spawn_local_stream_handler(move |req| async move {
            match req {
                DaemonRequest::GetRemoteControl { remote: _, target: _, responder } => {
                    responder.send(&mut Ok(())).unwrap();
                }
                DaemonRequest::EchoString { value, responder } => {
                    responder.send(&value).unwrap();
                }
                DaemonRequest::ListTargets { value: _, responder } => {
                    responder.send(&mut vec![].drain(..)).unwrap();
                }
                _ => {
                    assert!(false, format!("got unexpected request: {:?}", req));
                }
            }
        })
        .unwrap()
    }

    fn serve_responsive_rcs(server_end: ServerEnd<RemoteControlMarker>) {
        serve_stream::<RemoteControlMarker, _, _>(
            server_end.into_stream().unwrap(),
            move |req| async move {
                match req {
                    RemoteControlRequest::IdentifyHost { responder } => responder
                        .send(&mut Ok(IdentifyHostResponse {
                            addresses: Some(vec![]),
                            nodename: Some(NODENAME.to_string()),
                        }))
                        .unwrap(),
                    _ => panic!("Unexpected request: {:?}", req),
                }
            },
        );
    }
    fn serve_unresponsive_rcs(server_end: ServerEnd<RemoteControlMarker>) {
        serve_stream::<RemoteControlMarker, _, _>(
            server_end.into_stream().unwrap(),
            move |req| async move {
                match req {
                    RemoteControlRequest::IdentifyHost { responder: _ } => {
                        task::sleep(DEFAULT_RETRY_DELAY.add(Duration::from_millis(1000))).await;
                    }
                    _ => panic!("Unexpected request: {:?}", req),
                }
            },
        );
    }

    fn setup_responsive_daemon_server_with_target(responsive_rcs: bool) -> DaemonProxy {
        spawn_local_stream_handler(move |req| async move {
            match req {
                DaemonRequest::GetRemoteControl { remote, target: _, responder } => {
                    if responsive_rcs {
                        serve_responsive_rcs(remote);
                    } else {
                        serve_unresponsive_rcs(remote);
                    }
                    responder.send(&mut Ok(())).unwrap();
                }
                DaemonRequest::EchoString { value, responder } => {
                    responder.send(&value).unwrap();
                }
                DaemonRequest::ListTargets { value: _, responder } => {
                    responder
                        .send(
                            &mut vec![Target {
                                nodename: Some(NODENAME.to_string()),
                                addresses: Some(vec![]),
                                age_ms: Some(0),
                                rcs_state: Some(RemoteControlState::Unknown),
                                target_type: Some(TargetType::Unknown),
                                target_state: Some(TargetState::Unknown),
                            }]
                            .drain(..),
                        )
                        .unwrap();
                }
                _ => {
                    assert!(false, format!("got unexpected request: {:?}", req));
                }
            }
        })
        .unwrap()
    }
    fn setup_responsive_daemon_server_with_responsive_target() -> DaemonProxy {
        setup_responsive_daemon_server_with_target(true)
    }

    fn setup_responsive_daemon_server_with_unresponsive_target() -> DaemonProxy {
        setup_responsive_daemon_server_with_target(false)
    }

    fn setup_daemon_server_list_fails() -> DaemonProxy {
        spawn_local_stream_handler(move |req| async move {
            match req {
                DaemonRequest::GetRemoteControl { remote: _, target: _, responder: _ } => {
                    panic!("unexpected daemon call");
                }
                DaemonRequest::EchoString { value, responder } => {
                    responder.send(&value).unwrap();
                }
                DaemonRequest::ListTargets { value: _, responder: _ } => {
                    // Do nothing
                }
                _ => {
                    assert!(false, format!("got unexpected request: {:?}", req));
                }
            }
        })
        .unwrap()
    }

    fn setup_daemon_server_echo_times_out() -> DaemonProxy {
        spawn_local_stream_handler(move |req| async move {
            match req {
                DaemonRequest::GetRemoteControl { remote: _, target: _, responder: _ } => {
                    panic!("unexpected daemon call");
                }
                DaemonRequest::EchoString { value: _, responder: _ } => {
                    task::sleep(DEFAULT_RETRY_DELAY.add(Duration::from_millis(10))).await;
                }
                DaemonRequest::ListTargets { value: _, responder: _ } => {
                    panic!("unexpected daemon call");
                }
                _ => {
                    assert!(false, format!("got unexpected request: {:?}", req));
                }
            }
        })
        .unwrap()
    }

    fn verify_lines(output: &str, line_substrings: Vec<String>) {
        for (line, expected) in output.lines().zip(line_substrings.iter()) {
            if !expected.is_empty() {
                assert!(
                    line.contains(expected),
                    format!("'{}' does not contain expected string '{}'", line, expected)
                );
            }
        }
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_single_try_no_daemon_running_no_targets() {
        let fake = FakeDaemonManager::new(
            vec![false],
            vec![Ok(false)],
            vec![Ok(())],
            vec![Ok(setup_responsive_daemon_server())],
        );

        let mut output = String::new();
        {
            let mut writer = unsafe { BufWriter::new(output.as_mut_vec()) };
            doctor(&mut writer, &fake, 1, DEFAULT_RETRY_DELAY).await.unwrap();
        }

        print_full_output(&output);

        verify_lines(
            &output,
            vec![
                format!("{}{}", DAEMON_RUNNING_CHECK, NONE_RUNNING),
                format!("{}{}", KILLING_ZOMBIE_DAEMONS, NONE_RUNNING),
                format!("{}{}", SPAWNING_DAEMON, SUCCESS),
                format!("{}{}", CONNECTING_TO_DAEMON, SUCCESS),
                format!("{}{}", COMMUNICATING_WITH_DAEMON, SUCCESS),
                format!("{}{}", LISTING_TARGETS, SUCCESS),
                String::from("No targets found"),
                String::default(),
                BUG_URL.to_string(),
            ],
        );

        fake.assert_no_leftover_calls();
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_single_try_daemon_running_no_targets() {
        let fake = FakeDaemonManager::new(
            vec![true],
            vec![],
            vec![],
            vec![Ok(setup_responsive_daemon_server())],
        );

        let mut output = String::new();
        {
            let mut writer = unsafe { BufWriter::new(output.as_mut_vec()) };
            doctor(&mut writer, &fake, 1, DEFAULT_RETRY_DELAY).await.unwrap();
        }

        print_full_output(&output);

        verify_lines(
            &output,
            vec![
                format!("{}{}", DAEMON_RUNNING_CHECK, FOUND),
                format!("{}{}", CONNECTING_TO_DAEMON, SUCCESS),
                format!("{}{}", COMMUNICATING_WITH_DAEMON, SUCCESS),
                format!("{}{}", LISTING_TARGETS, SUCCESS),
                String::from("No targets found"),
                String::default(),
                BUG_URL.to_string(),
            ],
        );

        fake.assert_no_leftover_calls();
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_two_tries_daemon_running_list_fails() {
        let fake = FakeDaemonManager::new(
            vec![true],
            vec![],
            vec![],
            vec![Ok(setup_daemon_server_list_fails())],
        );

        let mut output = String::new();
        {
            let mut writer = unsafe { BufWriter::new(output.as_mut_vec()) };
            doctor(&mut writer, &fake, 2, DEFAULT_RETRY_DELAY).await.unwrap();
        }

        print_full_output(&output);

        verify_lines(
            &output,
            vec![
                format!("{}{}", DAEMON_RUNNING_CHECK, FOUND),
                format!("{}{}", CONNECTING_TO_DAEMON, SUCCESS),
                format!("{}{}", COMMUNICATING_WITH_DAEMON, SUCCESS),
                format!(
                    // TODO(jwing): Print error on a new line so that this
                    // doesn't have to match the entire error message.
                    "{}{}",
                    LISTING_TARGETS, FAILED_WITH_ERROR
                ),
                String::from("PEER_CLOSED"),
                String::default(),
                String::default(),
                String::from("PEER_CLOSED"),
                String::default(),
                String::default(),
                String::from("Attempt 2 of 2"),
                String::default(),
                String::from("PEER_CLOSED"),
                String::default(),
                String::default(),
                String::from("PEER_CLOSED"),
                String::default(),
                String::default(),
                String::from("Connecting to RCS failed"),
                String::default(),
                BUG_URL.to_string(),
            ],
        );

        fake.assert_no_leftover_calls();
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_two_tries_no_daemon_running_echo_timeout() {
        let fake = FakeDaemonManager::new(
            vec![false, true],
            vec![Ok(false), Ok(true)],
            vec![Ok(())],
            vec![Ok(setup_daemon_server_echo_times_out()), Ok(setup_responsive_daemon_server())],
        );

        let mut output = String::new();
        {
            let mut writer = unsafe { BufWriter::new(output.as_mut_vec()) };
            doctor(&mut writer, &fake, 2, DEFAULT_RETRY_DELAY).await.unwrap();
        }

        print_full_output(&output);

        verify_lines(
            &output,
            vec![
                format!("{}{}", DAEMON_RUNNING_CHECK, NONE_RUNNING),
                format!("{}{}", KILLING_ZOMBIE_DAEMONS, NONE_RUNNING),
                format!("{}{}", SPAWNING_DAEMON, SUCCESS),
                format!("{}{}", CONNECTING_TO_DAEMON, SUCCESS),
                format!("{}{}", COMMUNICATING_WITH_DAEMON, FAILED_TIMEOUT),
                String::default(),
                String::default(),
                String::from("Attempt 2 of 2"),
                format!("{}{}", DAEMON_RUNNING_CHECK, FOUND),
                format!("{}{}", CONNECTING_TO_DAEMON, SUCCESS),
                format!("{}{}", COMMUNICATING_WITH_DAEMON, SUCCESS),
                format!("{}{}", LISTING_TARGETS, SUCCESS),
                String::from("No targets found"),
                String::default(),
                String::default(),
                String::from("Attempt 2 of 2"),
                format!("{}{}", LISTING_TARGETS, SUCCESS),
                String::from("No targets found"),
                String::default(),
                BUG_URL.to_string(),
            ],
        );

        fake.assert_no_leftover_calls();
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_finds_target_connects_to_rcs() {
        let fake = FakeDaemonManager::new(
            vec![true],
            vec![],
            vec![],
            vec![Ok(setup_responsive_daemon_server_with_responsive_target())],
        );

        let mut output = String::new();
        {
            let mut writer = unsafe { BufWriter::new(output.as_mut_vec()) };
            doctor(&mut writer, &fake, 2, DEFAULT_RETRY_DELAY).await.unwrap();
        }

        print_full_output(&output);

        verify_lines(
            &output,
            vec![
                format!("{}{}", DAEMON_RUNNING_CHECK, FOUND),
                format!("{}{}", CONNECTING_TO_DAEMON, SUCCESS),
                format!("{}{}", COMMUNICATING_WITH_DAEMON, SUCCESS),
                format!("{}{}", LISTING_TARGETS, SUCCESS),
                format!("{}{}", CONNECTING_TO_RCS, SUCCESS),
                format!("{}{}", COMMUNICATING_WITH_RCS, SUCCESS),
                String::default(),
                String::default(),
                String::from(ALL_CHECKS_PASSED),
            ],
        );

        fake.assert_no_leftover_calls();
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_finds_target_connects_to_unresponsive_rcs() {
        let fake = FakeDaemonManager::new(
            vec![true],
            vec![],
            vec![],
            vec![Ok(setup_responsive_daemon_server_with_unresponsive_target())],
        );

        let mut output = String::new();
        {
            let mut writer = unsafe { BufWriter::new(output.as_mut_vec()) };
            doctor(&mut writer, &fake, 1, DEFAULT_RETRY_DELAY).await.unwrap();
        }

        print_full_output(&output);

        verify_lines(
            &output,
            vec![
                format!("{}{}", DAEMON_RUNNING_CHECK, FOUND),
                format!("{}{}", CONNECTING_TO_DAEMON, SUCCESS),
                format!("{}{}", COMMUNICATING_WITH_DAEMON, SUCCESS),
                format!("{}{}", LISTING_TARGETS, SUCCESS),
                format!("{}{}", CONNECTING_TO_RCS, SUCCESS),
                format!("{}{}", COMMUNICATING_WITH_RCS, FAILED_TIMEOUT),
                String::default(),
                String::default(),
                String::from("Connecting to RCS failed after maximum attempts"),
                String::default(),
                String::from(BUG_URL),
            ],
        );

        fake.assert_no_leftover_calls();
    }
}
