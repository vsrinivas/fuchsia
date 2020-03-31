// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![allow(unused)]
use {
    crate::args::{Ffx, Subcommand, TestCommand},
    crate::config::command::exec_config,
    crate::constants::{CONFIG_JSON_FILE, DAEMON, MAX_RETRY_COUNT},
    anyhow::{anyhow, format_err, Context, Error},
    ffx_daemon::{is_daemon_running, start as start_daemon},
    fidl::endpoints::{create_proxy, ServiceMarker},
    fidl_fidl_developer_bridge::{DaemonMarker, DaemonProxy},
    fidl_fuchsia_developer_remotecontrol::{ComponentControllerEvent, ComponentControllerMarker},
    fidl_fuchsia_overnet::ServiceConsumerProxyInterface,
    fidl_fuchsia_overnet_protocol::NodeId,
    fidl_fuchsia_test::{CaseIteratorMarker, Invocation, SuiteProxy},
    futures::{channel::mpsc, FutureExt, StreamExt, TryStreamExt},
    regex::Regex,
    signal_hook,
    std::env,
    std::io::{self, Write},
    std::process::Command,
    std::sync::{Arc, Mutex},
    test_executor::{run_and_collect_results_for_invocations as run_tests, TestEvent, TestResult},
};

mod args;
mod config;
mod constants;

// Cli
pub struct Cli<W: Write + Sync> {
    daemon_proxy: DaemonProxy,
    writer: W,
}

impl<W> Cli<W>
where
    W: Write + Sync,
{
    pub async fn new(writer: W) -> Result<Self, Error> {
        let mut peer_id = Cli::<W>::find_daemon().await?;
        let daemon_proxy = Cli::<W>::create_daemon_proxy(&mut peer_id).await?;
        Ok(Self { daemon_proxy, writer })
    }

    pub fn new_with_proxy(daemon_proxy: DaemonProxy, writer: W) -> Self {
        Self { daemon_proxy, writer }
    }

    async fn create_daemon_proxy(id: &mut NodeId) -> Result<DaemonProxy, Error> {
        let svc = hoist::connect_as_service_consumer()?;
        let (s, p) = fidl::Channel::create().context("failed to create zx channel")?;
        svc.connect_to_service(id, DaemonMarker::NAME, s)?;
        let proxy = fidl::AsyncChannel::from_channel(p).context("failed to make async channel")?;
        Ok(DaemonProxy::new(proxy))
    }

    async fn find_daemon() -> Result<NodeId, Error> {
        if !is_daemon_running() {
            Cli::<W>::spawn_daemon().await?;
        }
        let svc = hoist::connect_as_service_consumer()?;
        // Sometimes list_peers doesn't properly report the published services - retry a few times
        // but don't loop indefinitely.
        for _ in 0..MAX_RETRY_COUNT {
            let peers = svc.list_peers().await?;
            log::trace!("Got peers: {:?}", peers);
            for peer in peers {
                if peer.description.services.is_none() {
                    continue;
                }
                if peer
                    .description
                    .services
                    .unwrap()
                    .iter()
                    .find(|name| *name == DaemonMarker::NAME)
                    .is_none()
                {
                    continue;
                }
                return Ok(peer.id);
            }
        }
        panic!("No daemon found.")
    }

    pub async fn echo(&self, text: Option<String>) -> Result<String, Error> {
        match self
            .daemon_proxy
            .echo_string(match text {
                Some(ref t) => t,
                None => "Ffx",
            })
            .await
        {
            Ok(r) => {
                log::info!("SUCCESS: received {:?}", r);
                return Ok(r);
            }
            Err(e) => panic!("ERROR: {:?}", e),
        }
    }

    pub async fn list_targets(&self, text: Option<String>) -> Result<String, Error> {
        match self
            .daemon_proxy
            .list_targets(match text {
                Some(ref t) => t,
                None => "",
            })
            .await
        {
            Ok(r) => {
                log::info!("SUCCESS: received {:?}", r);
                return Ok(r);
            }
            Err(e) => panic!("ERROR: {:?}", e),
        }
    }

    pub async fn run_component(&self, url: String, args: &Vec<String>) -> Result<(), Error> {
        let (proxy, server_end) = create_proxy::<ComponentControllerMarker>()?;
        let (sout, cout) =
            fidl::Socket::create(fidl::SocketOpts::STREAM).context("failed to create socket")?;
        let (serr, cerr) =
            fidl::Socket::create(fidl::SocketOpts::STREAM).context("failed to create socket")?;

        // This is only necessary until Overnet correctly handle setup for passed channels.
        // TODO(jwing) remove this once that is finished.
        proxy.ping();

        // TODO(fxb/49063): Can't use the self.writer in these threads due to static lifetime.
        let out_thread = std::thread::spawn(move || loop {
            let mut buf = [0u8; 128];
            let n = cout.read(&mut buf).or::<usize>(Ok(0usize)).unwrap();
            if n > 0 {
                print!("{}", String::from_utf8_lossy(&buf));
            }
        });

        let err_thread = std::thread::spawn(move || loop {
            let mut buf = [0u8; 128];
            let n = cerr.read(&mut buf).or::<usize>(Ok(0usize)).unwrap();
            if n > 0 {
                eprint!("{}", String::from_utf8_lossy(&buf));
            }
        });

        let event_stream = proxy.take_event_stream();
        let term_thread = std::thread::spawn(move || {
            let mut e = event_stream.take(1usize);
            while let Some(result) = futures::executor::block_on(e.next()) {
                match result {
                    Ok(ComponentControllerEvent::OnTerminated { exit_code }) => {
                        println!("Component exited with exit code: {}", exit_code);
                        match exit_code {
                            -1 => println!("This exit code may mean that the specified package doesn't exist.\
                                        \nCheck that the package is in your universe (`fx set --with ...`) and that `fx serve` is running."),
                            _ => {},
                        };
                        break;
                    }
                    Err(err) => {
                        eprintln!("error reading component controller events. Component termination may not be detected correctly. {} ", err);
                    }
                }
            }
        });

        let kill_arc = Arc::new(Mutex::new(false));
        let arc_mut = kill_arc.clone();
        unsafe {
            signal_hook::register(signal_hook::SIGINT, move || {
                let mut kill_started = arc_mut.lock().unwrap();
                if !*kill_started {
                    println!("\nCaught interrupt, killing remote component.");
                    proxy.kill();
                    *kill_started = true;
                } else {
                    // If for some reason the kill signal hangs, we want to give the user
                    // a way to exit ffx.
                    println!("Received second interrupt. Forcing exit...");
                    std::process::exit(0);
                }
            });
        }

        let _result = self
            .daemon_proxy
            .start_component(&url, &mut args.iter().map(|s| s.as_str()), sout, serr, server_end)
            .await?;
        term_thread.join().unwrap();

        Ok(())
    }

    async fn get_tests(&mut self, suite_url: &String) -> Result<(), Error> {
        let (suite_proxy, suite_server_end) = fidl::endpoints::create_proxy().unwrap();
        let (_controller_proxy, controller_server_end) = fidl::endpoints::create_proxy().unwrap();

        log::info!("launching test suite {}", suite_url);

        self.daemon_proxy
            .launch_suite(&suite_url, suite_server_end, controller_server_end)
            .await
            .context("launch_test call failed")?
            .map_err(|e| format_err!("error launching test: {:?}", e))?;

        log::info!("launched suite, getting tests");

        let (case_iterator, test_server_end) = create_proxy::<CaseIteratorMarker>()?;
        suite_proxy
            .get_tests(test_server_end)
            .map_err(|e| format_err!("Error getting test steps: {}", e))?;

        loop {
            let cases = case_iterator.get_next().await?;
            if cases.is_empty() {
                return Ok(());
            }
            writeln!(self.writer, "Tests in suite {}:\n", suite_url);
            for case in cases {
                match case.name {
                    Some(n) => writeln!(self.writer, "{}", n),
                    None => writeln!(self.writer, "<No name>"),
                };
            }
        }
    }

    async fn get_invocations(
        &self,
        suite: &SuiteProxy,
        test_selector: &Option<Regex>,
    ) -> Result<Vec<Invocation>, Error> {
        let (case_iterator, server_end) = fidl::endpoints::create_proxy()?;
        suite.get_tests(server_end).map_err(|e| format_err!("Error getting test steps: {}", e))?;

        let mut invocations = Vec::<Invocation>::new();
        loop {
            let cases = case_iterator.get_next().await?;
            if cases.is_empty() {
                break;
            }
            for case in cases {
                // TODO: glob type pattern matching would probably be better than regex - maybe
                // both? Will update after meeting with UX.
                let test_case_name = case.name.unwrap();
                match &test_selector {
                    Some(s) => {
                        if (s.is_match(&test_case_name)) {
                            invocations.push(Invocation { name: Some(test_case_name), tag: None });
                        }
                    }
                    None => invocations.push(Invocation { name: Some(test_case_name), tag: None }),
                }
            }
        }
        Ok(invocations)
    }

    async fn run_tests(&mut self, suite_url: &String, tests: &Option<String>) -> Result<(), Error> {
        let (suite_proxy, suite_server_end) =
            fidl::endpoints::create_proxy().expect("creating suite proxy");
        let (_controller_proxy, controller_server_end) =
            fidl::endpoints::create_proxy().expect("creating controller proxy");

        let test_selector = match tests {
            Some(s) => match Regex::new(s) {
                Ok(r) => Some(r),
                Err(e) => {
                    return Err(anyhow!("invalid regex for tests: \"{}\"\n{}", s, e));
                }
            },
            None => None,
        };

        log::info!("launching test suite {}", suite_url);
        writeln!(self.writer, "*** Launching {} ***", suite_url);

        self.daemon_proxy
            .launch_suite(&suite_url, suite_server_end, controller_server_end)
            .await
            .context("launch_test call failed")?
            .map_err(|e| format_err!("error launching test: {:?}", e))?;

        log::info!("launched suite, getting tests");
        let (sender, recv) = mpsc::channel(1);

        writeln!(self.writer, "Getting tests...");
        let invocations = self.get_invocations(&suite_proxy, &test_selector).await?;
        if (invocations.is_empty()) {
            match tests {
                Some(test_selector) => {
                    writeln!(self.writer, "No test cases match {}", test_selector)
                }
                None => writeln!(self.writer, "No tests cases found in suite {}", suite_url),
            };
            return Ok(());
        }
        writeln!(self.writer, "Running tests...");
        let (remote, test_fut) =
            run_tests(suite_proxy, sender, suite_url.to_string(), invocations).remote_handle();
        hoist::spawn(remote);
        let successful_completion = self.collect_events(recv).await;
        test_fut.await.map_err(|e| format_err!("Error running test: {}", e))?;

        if !successful_completion {
            return Err(anyhow!("Test run finished prematurely. Something went wrong."));
        }
        writeln!(self.writer, "*** Finished {} ***", suite_url);
        Ok(())
    }

    async fn collect_events(&mut self, mut recv: mpsc::Receiver<TestEvent>) -> bool {
        let mut successful_completion = false;
        while let Some(event) = recv.next().await {
            match event {
                TestEvent::LogMessage { test_case_name, msg } => {
                    let logs = msg.split("\n");
                    for log in logs {
                        if log.len() > 0 {
                            writeln!(self.writer, "{}: {}", test_case_name, log.to_string());
                        }
                    }
                }
                TestEvent::TestCaseStarted { test_case_name } => {
                    writeln!(self.writer, "[RUNNING]\t{}", test_case_name);
                }
                TestEvent::TestCaseFinished { test_case_name, result } => {
                    match result {
                        TestResult::Passed => writeln!(self.writer, "[PASSED]\t{}", test_case_name),
                        TestResult::Failed => writeln!(self.writer, "[FAILED]\t{}", test_case_name),
                        TestResult::Skipped => {
                            writeln!(self.writer, "[SKIPPED]\t{}", test_case_name)
                        }
                        TestResult::Error => writeln!(self.writer, "[ERROR]\t{}", test_case_name),
                    };
                }
                TestEvent::Finish => {
                    successful_completion = true;
                }
            };
        }
        successful_completion
    }

    pub async fn test(&mut self, test: TestCommand) -> Result<(), Error> {
        if (test.list) {
            self.get_tests(&test.url).await
        } else {
            self.run_tests(&test.url, &test.tests).await
        }
    }

    async fn spawn_daemon() -> Result<(), Error> {
        Command::new(env::current_exe().unwrap()).arg(DAEMON).spawn()?;
        Ok(())
    }
}

////////////////////////////////////////////////////////////////////////////////
// main

async fn async_main() -> Result<(), Error> {
    let app: Ffx = argh::from_env();
    let writer = Box::new(io::stdout());
    match app.subcommand {
        Subcommand::Echo(c) => {
            match Cli::new(writer).await?.echo(c.text).await {
                Ok(r) => {
                    println!("SUCCESS: received {:?}", r);
                }
                Err(e) => {
                    println!("ERROR: {:?}", e);
                }
            }
            Ok(())
        }
        Subcommand::List(c) => {
            match Cli::new(writer).await?.list_targets(c.nodename).await {
                Ok(r) => {
                    println!("SUCCESS: received {:?}", r);
                }
                Err(e) => {
                    println!("ERROR: {:?}", e);
                }
            }
            Ok(())
        }
        Subcommand::RunComponent(c) => {
            match Cli::new(writer).await?.run_component(c.url, &c.args).await {
                Ok(r) => {}
                Err(e) => {
                    println!("ERROR: {:?}", e);
                }
            }
            Ok(())
        }
        Subcommand::Daemon(_) => start_daemon().await,
        Subcommand::Config(c) => exec_config(c),
        Subcommand::Test(t) => {
            match Cli::new(writer).await.unwrap().test(t).await {
                Ok(_) => {
                    log::info!("Test successfully run");
                }
                Err(e) => {
                    println!("ERROR: {:?}", e);
                }
            }
            Ok(())
        }
    }
}

fn main() {
    hoist::run(async move {
        async_main().await.map_err(|e| println!("{}", e)).expect("could not start ffx");
    })
}

////////////////////////////////////////////////////////////////////////////////
// tests

#[cfg(test)]
mod test {
    use super::*;
    use fidl_fidl_developer_bridge::{DaemonMarker, DaemonProxy, DaemonRequest};
    use fidl_fuchsia_test::{
        Case, CaseIteratorRequest, CaseIteratorRequestStream, CaseListenerMarker, Result_, Status,
        SuiteRequest, SuiteRequestStream,
    };
    use std::io::BufWriter;

    fn spawn_fake_iterator_server(values: Vec<String>, mut stream: CaseIteratorRequestStream) {
        let mut iter = values.into_iter().map(|name| Case { name: Some(name) });
        hoist::spawn(async move {
            while let Ok(Some(CaseIteratorRequest::GetNext { responder })) = stream.try_next().await
            {
                responder.send(&mut iter.by_ref().take(50));
            }
        });
    }

    fn spawn_fake_suite_server(mut stream: SuiteRequestStream, num_tests: usize) {
        hoist::spawn(async move {
            while let Ok(req) = stream.try_next().await {
                match req {
                    Some(SuiteRequest::GetTests { iterator, control_handle: _ }) => {
                        let values: Vec<String> =
                            (0..num_tests).map(|i| format!("Test {}", i)).collect();
                        let iterator_request_stream = iterator.into_stream().unwrap();
                        spawn_fake_iterator_server(values, iterator_request_stream);
                    }
                    Some(SuiteRequest::Run { mut tests, options: _, listener, .. }) => {
                        let listener = listener
                            .into_proxy()
                            .context("Can't convert listener into proxy")
                            .unwrap();
                        tests.iter_mut().for_each(|t| {
                            let (log, client_log) =
                                fidl::Socket::create(fidl::SocketOpts::DATAGRAM)
                                    .context("failed to create socket")
                                    .unwrap();
                            let (case_listener, client_end) =
                                create_proxy::<CaseListenerMarker>().unwrap();
                            listener
                                .on_test_case_started(
                                    Invocation { name: t.name.take(), tag: None },
                                    client_log,
                                    client_end,
                                )
                                .context("Cannot send on_test_case_started")
                                .unwrap();
                            log.write(b"Test log message\n");
                            case_listener
                                .finished(Result_ { status: Some(Status::Passed) })
                                .context("Cannot send finished")
                                .unwrap();
                        });
                        listener.on_finished().context("Cannot send on_finished event").unwrap();
                    }
                    _ => assert!(false),
                }
            }
        });
    }

    fn setup_fake_daemon_service() -> DaemonProxy {
        setup_fake_daemon_service_with_tests(0)
    }

    fn setup_fake_daemon_service_with_tests(num_tests: usize) -> DaemonProxy {
        let (proxy, mut stream) =
            fidl::endpoints::create_proxy_and_stream::<DaemonMarker>().unwrap();

        hoist::spawn(async move {
            while let Ok(req) = stream.try_next().await {
                match req {
                    Some(DaemonRequest::EchoString { value, responder }) => {
                        let _ = responder.send(value.as_ref());
                    }
                    Some(DaemonRequest::StartComponent {
                        component_url,
                        args,
                        component_stdout: _,
                        component_stderr: _,
                        controller: _,
                        responder,
                    }) => {
                        let _ = responder.send(&mut Ok(()));
                    }
                    Some(DaemonRequest::LaunchSuite { test_url, suite, controller, responder }) => {
                        let suite_request_stream = suite.into_stream().unwrap();
                        spawn_fake_suite_server(suite_request_stream, num_tests);
                        let _ = responder.send(&mut Ok(()));
                    }
                    _ => assert!(false),
                }
            }
        });

        proxy
    }

    #[test]
    fn test_echo() {
        let mut output = String::new();
        let echo = "test-echo";
        hoist::run(async move {
            let writer = unsafe { BufWriter::new(output.as_mut_vec()) };
            let echoed = Cli::new_with_proxy(setup_fake_daemon_service(), writer)
                .echo(Some(echo.to_string()))
                .await
                .unwrap();
            assert_eq!(echoed, echo);
        });
    }

    #[test]
    fn test_run_component() -> Result<(), Error> {
        let mut output = String::new();
        let url = "fuchsia-pkg://fuchsia.com/test#meta/test.cmx";
        let args = vec!["test1".to_string(), "test2".to_string()];
        let (daemon_proxy, stream) = fidl::endpoints::create_proxy_and_stream::<DaemonMarker>()?;
        let (_, server_end) = create_proxy::<ComponentControllerMarker>()?;
        let (sout, _) =
            fidl::Socket::create(fidl::SocketOpts::STREAM).context("failed to create socket")?;
        let (serr, _) =
            fidl::Socket::create(fidl::SocketOpts::STREAM).context("failed to create socket")?;
        hoist::run(async move {
            let writer = unsafe { BufWriter::new(output.as_mut_vec()) };
            // There isn't a lot we can test here right now since this method has an empty response.
            // We just check for an Ok(()) and leave it to a real integration to test behavior.
            let response = Cli::new_with_proxy(setup_fake_daemon_service(), writer)
                .run_component(url.to_string(), &args)
                .await
                .unwrap();
        });

        Ok(())
    }

    #[test]
    fn test_list_tests() -> Result<(), Error> {
        let mut output = String::new();
        let url = "fuchsia-pkg://fuchsia.com/gtest_adapter_echo_example#meta/echo_test_realm.cm"
            .to_string();
        let cmd = TestCommand { url, devices: None, list: true, tests: None };
        hoist::run(async move {
            let writer = unsafe { BufWriter::new(output.as_mut_vec()) };
            let num_tests = 50;
            let response =
                Cli::new_with_proxy(setup_fake_daemon_service_with_tests(num_tests), writer)
                    .test(cmd)
                    .await
                    .unwrap();
            assert_eq!(response, ());
            let test = Regex::new(r"Test [0-9+]").expect("test regex");
            assert_eq!(num_tests, test.find_iter(&output).count());
        });
        Ok(())
    }

    fn run_tests(
        num_tests: usize,
        expected_run: usize,
        selector: Option<String>,
    ) -> Result<(), Error> {
        let mut output = String::new();
        let url = "fuchsia-pkg://fuchsia.com/gtest_adapter_echo_example#meta/echo_test_realm.cm"
            .to_string();
        let cmd = TestCommand { url, devices: None, list: false, tests: selector };
        hoist::run(async move {
            let writer = unsafe { BufWriter::new(output.as_mut_vec()) };
            let response =
                Cli::new_with_proxy(setup_fake_daemon_service_with_tests(num_tests), writer)
                    .test(cmd)
                    .await
                    .unwrap();
            assert_eq!(response, ());
            let test_running = Regex::new(r"RUNNING").expect("test regex");
            assert_eq!(expected_run, test_running.find_iter(&output).count());
            let test_passed = Regex::new(r"PASSED").expect("test regex");
            assert_eq!(expected_run, test_passed.find_iter(&output).count());
        });
        Ok(())
    }

    #[test]
    fn test_run_tests() -> Result<(), Error> {
        run_tests(100, 100, None)
    }

    #[test]
    fn test_run_tests_with_selector() -> Result<(), Error> {
        run_tests(100, 19, Some("6".to_string()))
    }

    #[test]
    fn test_run_tests_with_unmatched_selector() -> Result<(), Error> {
        run_tests(100, 0, Some("Echo".to_string()))
    }

    #[test]
    fn test_run_tests_with_invalid_selector() -> Result<(), Error> {
        let mut output = String::new();
        let url = "fuchsia-pkg://fuchsia.com/gtest_adapter_echo_example#meta/echo_test_realm.cm"
            .to_string();
        let cmd = TestCommand { url, devices: None, list: false, tests: Some("[".to_string()) };
        hoist::run(async move {
            let writer = unsafe { BufWriter::new(output.as_mut_vec()) };
            let response = Cli::new_with_proxy(setup_fake_daemon_service_with_tests(100), writer)
                .test(cmd)
                .await;
            assert!(response.is_err());
        });
        Ok(())
    }
}
