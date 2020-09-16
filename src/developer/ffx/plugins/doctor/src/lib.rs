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
    fidl_fuchsia_developer_bridge::{DaemonProxy, Target},
    fidl_fuchsia_developer_remotecontrol::RemoteControlMarker,
    std::collections::HashSet,
    std::io::{stdout, Write},
    std::time::Duration,
    termion::{color, style},
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

    let ffx: ffx_lib_args::Ffx = argh::from_env();
    let target_str = ffx.target.unwrap_or(String::default());

    doctor(
        &mut writer,
        &daemon_manager,
        &target_str,
        cmd.retry_count,
        delay,
        cmd.force_daemon_restart,
    )
    .await
}

async fn doctor<W: Write>(
    writer: &mut W,
    daemon_manager: &impl DaemonManager,
    target_str: &str,
    retry_count: usize,
    retry_delay: Duration,
    force_daemon_restart: bool,
) -> Result<()> {
    let mut proxy_opt: Option<DaemonProxy> = None;
    let mut targets_opt: Option<Vec<Target>> = None;
    writeln!(writer, "{}{}{}", style::Bold, DAEMON_CHECK_INTRO, style::Reset).unwrap();
    for i in 0..retry_count {
        proxy_opt = None;
        if i > 0 {
            daemon_manager.kill_all().await?;
            writeln!(writer, "\n\nAttempt {} of {}", i + 1, retry_count).unwrap();
        } else if force_daemon_restart {
            writeln!(writer, "{}", FORCE_DAEMON_RESTART_MESSAGE).unwrap();
            daemon_manager.kill_all().await?;
        }

        print_status_line(writer, DAEMON_RUNNING_CHECK);
        if !daemon_manager.is_running().await {
            writeln!(writer, "{}", NONE_RUNNING).unwrap();
            print_status_line(writer, KILLING_ZOMBIE_DAEMONS);

            if daemon_manager.kill_all().await? {
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

        let status_str;
        if target_str.is_empty() {
            status_str = String::from(LISTING_TARGETS_NO_FILTER);
        } else {
            status_str = format!("Attempting to list targets with filter '{}'...", target_str);
        }

        print_status_line(writer, &status_str);
        targets_opt = match timeout(
            retry_delay,
            proxy_opt.as_ref().unwrap().list_targets(target_str),
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
            Ok(t) => {
                writeln!(writer, "{}", SUCCESS).unwrap();
                Some(t.unwrap())
            }
        };

        if targets_opt.is_some() && targets_opt.as_ref().unwrap().len() == 0 {
            writeln!(writer, "{}", NO_TARGETS_FOUND_SHORT).unwrap();
            continue;
        }

        break;
    }

    if proxy_opt.is_none() {
        writeln!(
            writer,
            "\n{}{}{}{}",
            style::Bold,
            color::Fg(color::Red),
            DAEMON_CHECKS_FAILED,
            style::Reset
        )
        .unwrap();
        writeln!(writer, "Bug link: {}", BUG_URL).unwrap();
        return Ok(());
    }

    if targets_opt.is_none() || targets_opt.as_ref().unwrap().len() == 0 {
        writeln!(writer, "\n{}", NO_TARGETS_FOUND_EXTENDED).unwrap();
        writeln!(writer, "Bug link: {}", BUG_URL).unwrap();
        return Ok(());
    }

    let targets = targets_opt.unwrap();
    let daemon = proxy_opt.take().unwrap();
    let mut successful_targets = HashSet::new();

    for target in targets.iter() {
        writeln!(
            writer,
            "{}\nChecking target: '{}'. {}{}",
            style::Bold,
            target.nodename.as_ref().unwrap_or(&"UNKNOWN".to_string()),
            TARGET_CHOICE_HELP,
            style::Reset
        )
        .unwrap();
        for i in 0..retry_count {
            if i > 0 {
                writeln!(writer, "\n\nAttempt {} of {}", i + 1, retry_count).unwrap();
            }

            // TODO(jwing): SSH into the device and kill Overnet+RCS if anything below this fails
            print_status_line(writer, CONNECTING_TO_RCS);
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
                    successful_targets.insert(target.nodename.as_ref().unwrap()).to_string();
                    break;
                }
            };
        }
    }

    writeln!(writer, "{}", style::Bold).unwrap();
    writeln!(writer, "{}", TARGET_SUMMARY).unwrap();
    for target in targets.iter() {
        let nodename = target.nodename.as_ref().unwrap();
        if successful_targets.contains(&nodename.clone()) {
            writeln!(writer, "{}✓ {}", color::Fg(color::Green), nodename).unwrap();
        } else {
            writeln!(writer, "{}✗ {}", color::Fg(color::Red), nodename).unwrap();
        }
    }
    writeln!(writer, "{}", style::Reset).unwrap();

    if targets.len() != successful_targets.len() {
        writeln!(writer, "{}", RCS_TERMINAL_FAILURE).unwrap();
        writeln!(writer, "{}", RCS_TERMINAL_FAILURE_BUG_INSTRUCTIONS).unwrap();
        writeln!(writer, "{}", BUG_URL).unwrap();
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
    const UNRESPONSIVE_NODENAME: &str = "fake-nodename-unresponsive";
    const NON_EXISTENT_NODENAME: &str = "extra-fake-nodename";
    const LISTING_TARGETS_WITH_FILTER: &str =
        "Attempting to list targets with filter 'fake-nodename'...";
    const LISTING_TARGETS_WITH_FAKE_FILTER: &str =
        "Attempting to list targets with filter 'extra-fake-nodename'...";
    const CHOSE_TARGET: &str = "Checking target: 'fake-nodename'. ";
    const CHOSE_UNRESPONSIVE_TARGET: &str = "Checking target: 'fake-nodename-unresponsive'. ";
    const SUCCESSFUL_TARGET: &str = "✓ fake-nodename";
    const FAILED_TARGET: &str = "✗ fake-nodename-unresponsive";
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
        async fn kill_all(&self) -> Result<bool> {
            let mut state = self.state_manager.lock().unwrap();
            assert!(!state.kill_results.is_empty(), "too many calls to kill_all");
            state.kill_results.remove(0)
        }

        async fn is_running(&self) -> bool {
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

    fn setup_responsive_daemon_server_with_targets() -> DaemonProxy {
        spawn_local_stream_handler(move |req| async move {
            match req {
                DaemonRequest::GetRemoteControl { remote, target, responder } => {
                    if target == NODENAME {
                        serve_responsive_rcs(remote);
                    } else if target == UNRESPONSIVE_NODENAME {
                        serve_unresponsive_rcs(remote);
                    } else {
                        panic!("got unexpected target string: '{}'", target);
                    }
                    responder.send(&mut Ok(())).unwrap();
                }
                DaemonRequest::EchoString { value, responder } => {
                    responder.send(&value).unwrap();
                }
                DaemonRequest::ListTargets { value, responder } => {
                    if !value.is_empty() && value != NODENAME && value != UNRESPONSIVE_NODENAME {
                        responder.send(&mut vec![].drain(..)).unwrap();
                    } else if value == NODENAME {
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
                    } else {
                        responder
                            .send(
                                &mut vec![
                                    Target {
                                        nodename: Some(NODENAME.to_string()),
                                        addresses: Some(vec![]),
                                        age_ms: Some(0),
                                        rcs_state: Some(RemoteControlState::Unknown),
                                        target_type: Some(TargetType::Unknown),
                                        target_state: Some(TargetState::Unknown),
                                    },
                                    Target {
                                        nodename: Some(UNRESPONSIVE_NODENAME.to_string()),
                                        addresses: Some(vec![]),
                                        age_ms: Some(0),
                                        rcs_state: Some(RemoteControlState::Unknown),
                                        target_type: Some(TargetType::Unknown),
                                        target_state: Some(TargetState::Unknown),
                                    },
                                ]
                                .drain(..),
                            )
                            .unwrap();
                    }
                }
                _ => {
                    assert!(false, format!("got unexpected request: {:?}", req));
                }
            }
        })
        .unwrap()
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

        // Verify that there aren't any additional lines in the actual output that didn't get
        // compared in the previous loop.
        assert!(output.lines().collect::<Vec<_>>().len() <= line_substrings.len());
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
            doctor(&mut writer, &fake, "", 1, DEFAULT_RETRY_DELAY, false).await.unwrap();
        }

        print_full_output(&output);

        verify_lines(
            &output,
            vec![
                format!("{}", DAEMON_CHECK_INTRO),
                format!("{}{}", DAEMON_RUNNING_CHECK, NONE_RUNNING),
                format!("{}{}", KILLING_ZOMBIE_DAEMONS, NONE_RUNNING),
                format!("{}{}", SPAWNING_DAEMON, SUCCESS),
                format!("{}{}", CONNECTING_TO_DAEMON, SUCCESS),
                format!("{}{}", COMMUNICATING_WITH_DAEMON, SUCCESS),
                format!("{}{}", LISTING_TARGETS_NO_FILTER, SUCCESS),
                String::from("No targets found"),
                String::default(),
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
            doctor(&mut writer, &fake, "", 1, DEFAULT_RETRY_DELAY, false).await.unwrap();
        }

        print_full_output(&output);

        verify_lines(
            &output,
            vec![
                format!("{}", DAEMON_CHECK_INTRO),
                format!("{}{}", DAEMON_RUNNING_CHECK, FOUND),
                format!("{}{}", CONNECTING_TO_DAEMON, SUCCESS),
                format!("{}{}", COMMUNICATING_WITH_DAEMON, SUCCESS),
                format!("{}{}", LISTING_TARGETS_NO_FILTER, SUCCESS),
                String::from("No targets found"),
                String::default(),
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
            vec![true, false],
            vec![Ok(true), Ok(false)],
            vec![Ok(())],
            vec![Ok(setup_daemon_server_list_fails()), Ok(setup_daemon_server_list_fails())],
        );

        let mut output = String::new();
        {
            let mut writer = unsafe { BufWriter::new(output.as_mut_vec()) };
            doctor(&mut writer, &fake, "", 2, DEFAULT_RETRY_DELAY, false).await.unwrap();
        }

        print_full_output(&output);

        verify_lines(
            &output,
            vec![
                format!("{}", DAEMON_CHECK_INTRO),
                format!("{}{}", DAEMON_RUNNING_CHECK, FOUND),
                format!("{}{}", CONNECTING_TO_DAEMON, SUCCESS),
                format!("{}{}", COMMUNICATING_WITH_DAEMON, SUCCESS),
                format!("{}{}", LISTING_TARGETS_NO_FILTER, FAILED_WITH_ERROR),
                String::from("PEER_CLOSED"),
                String::default(),
                String::default(),
                String::from("PEER_CLOSED"),
                String::default(),
                String::default(),
                String::from("Attempt 2 of 2"),
                format!("{}{}", DAEMON_RUNNING_CHECK, NONE_RUNNING),
                format!("{}{}", KILLING_ZOMBIE_DAEMONS, NONE_RUNNING),
                format!("{}{}", SPAWNING_DAEMON, SUCCESS),
                format!("{}{}", CONNECTING_TO_DAEMON, SUCCESS),
                format!("{}{}", COMMUNICATING_WITH_DAEMON, SUCCESS),
                format!("{}{}", LISTING_TARGETS_NO_FILTER, FAILED_WITH_ERROR),
                String::from("PEER_CLOSED"),
                String::default(),
                String::default(),
                String::from("PEER_CLOSED"),
                String::default(),
                String::from("No targets found"),
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
            doctor(&mut writer, &fake, "", 2, DEFAULT_RETRY_DELAY, false).await.unwrap();
        }

        print_full_output(&output);

        verify_lines(
            &output,
            vec![
                format!("{}", DAEMON_CHECK_INTRO),
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
                format!("{}{}", LISTING_TARGETS_NO_FILTER, SUCCESS),
                String::from("No targets found"),
                String::default(),
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
            vec![Ok(setup_responsive_daemon_server_with_targets())],
        );

        let mut output = String::new();
        {
            let mut writer = unsafe { BufWriter::new(output.as_mut_vec()) };
            doctor(&mut writer, &fake, "", 1, DEFAULT_RETRY_DELAY, false).await.unwrap();
        }

        print_full_output(&output);

        verify_lines(
            &output,
            vec![
                format!("{}", DAEMON_CHECK_INTRO),
                format!("{}{}", DAEMON_RUNNING_CHECK, FOUND),
                format!("{}{}", CONNECTING_TO_DAEMON, SUCCESS),
                format!("{}{}", COMMUNICATING_WITH_DAEMON, SUCCESS),
                format!("{}{}", LISTING_TARGETS_NO_FILTER, SUCCESS),
                String::default(),
                format!("{}{}", CHOSE_TARGET, TARGET_CHOICE_HELP),
                format!("{}{}", CONNECTING_TO_RCS, SUCCESS),
                format!("{}{}", COMMUNICATING_WITH_RCS, SUCCESS),
                String::default(),
                format!("{}{}", CHOSE_UNRESPONSIVE_TARGET, TARGET_CHOICE_HELP),
                format!("{}{}", CONNECTING_TO_RCS, SUCCESS),
                format!("{}{}", COMMUNICATING_WITH_RCS, FAILED_TIMEOUT),
                String::default(),
                String::from(TARGET_SUMMARY),
                String::from(SUCCESSFUL_TARGET),
                String::from(FAILED_TARGET),
                String::default(),
                String::from(RCS_TERMINAL_FAILURE),
                String::from(RCS_TERMINAL_FAILURE_BUG_INSTRUCTIONS),
                String::from(BUG_URL),
            ],
        );

        fake.assert_no_leftover_calls();
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_finds_target_with_filter() {
        let fake = FakeDaemonManager::new(
            vec![true],
            vec![],
            vec![],
            vec![Ok(setup_responsive_daemon_server_with_targets())],
        );

        let mut output = String::new();
        {
            let mut writer = unsafe { BufWriter::new(output.as_mut_vec()) };
            doctor(&mut writer, &fake, &NODENAME, 2, DEFAULT_RETRY_DELAY, false).await.unwrap();
        }

        print_full_output(&output);

        verify_lines(
            &output,
            vec![
                String::from(DAEMON_CHECK_INTRO),
                format!("{}{}", DAEMON_RUNNING_CHECK, FOUND),
                format!("{}{}", CONNECTING_TO_DAEMON, SUCCESS),
                format!("{}{}", COMMUNICATING_WITH_DAEMON, SUCCESS),
                format!("{}{}", LISTING_TARGETS_WITH_FILTER, SUCCESS),
                String::default(),
                format!("{}{}", CHOSE_TARGET, TARGET_CHOICE_HELP),
                format!("{}{}", CONNECTING_TO_RCS, SUCCESS),
                format!("{}{}", COMMUNICATING_WITH_RCS, SUCCESS),
                String::default(),
                String::from(TARGET_SUMMARY),
                String::from(SUCCESSFUL_TARGET),
                String::default(),
            ],
        );

        fake.assert_no_leftover_calls();
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_invalid_filter_finds_no_targets() {
        let fake = FakeDaemonManager::new(
            vec![true],
            vec![],
            vec![],
            vec![Ok(setup_responsive_daemon_server_with_targets())],
        );

        let mut output = String::new();
        {
            let mut writer = unsafe { BufWriter::new(output.as_mut_vec()) };
            doctor(&mut writer, &fake, &NON_EXISTENT_NODENAME, 1, DEFAULT_RETRY_DELAY, false)
                .await
                .unwrap();
        }

        print_full_output(&output);

        verify_lines(
            &output,
            vec![
                format!("{}", DAEMON_CHECK_INTRO),
                format!("{}{}", DAEMON_RUNNING_CHECK, FOUND),
                format!("{}{}", CONNECTING_TO_DAEMON, SUCCESS),
                format!("{}{}", COMMUNICATING_WITH_DAEMON, SUCCESS),
                format!("{}{}", LISTING_TARGETS_WITH_FAKE_FILTER, SUCCESS),
                String::from("No targets found"),
                String::default(),
                String::from("No targets found"),
                String::default(),
                String::from(BUG_URL),
            ],
        );

        fake.assert_no_leftover_calls();
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_single_try_daemon_running_force_restart() {
        let fake = FakeDaemonManager::new(
            vec![false],
            vec![Ok(true), Ok(false)],
            vec![Ok(())],
            vec![Ok(setup_responsive_daemon_server())],
        );

        let mut output = String::new();
        {
            let mut writer = unsafe { BufWriter::new(output.as_mut_vec()) };
            doctor(&mut writer, &fake, "", 1, DEFAULT_RETRY_DELAY, true).await.unwrap();
        }

        print_full_output(&output);

        verify_lines(
            &output,
            vec![
                format!("{}", DAEMON_CHECK_INTRO),
                String::from(FORCE_DAEMON_RESTART_MESSAGE),
                format!("{}{}", DAEMON_RUNNING_CHECK, NONE_RUNNING),
                format!("{}{}", KILLING_ZOMBIE_DAEMONS, NONE_RUNNING),
                format!("{}{}", SPAWNING_DAEMON, SUCCESS),
                format!("{}{}", CONNECTING_TO_DAEMON, SUCCESS),
                format!("{}{}", COMMUNICATING_WITH_DAEMON, SUCCESS),
                format!("{}{}", LISTING_TARGETS_NO_FILTER, SUCCESS),
                String::from("No targets found"),
                String::default(),
                String::from("No targets found"),
                String::default(),
                BUG_URL.to_string(),
            ],
        );

        fake.assert_no_leftover_calls();
    }
}
