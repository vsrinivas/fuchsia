// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{bail, Context as _, Error, Result},
    diagnostics_data::LogsData,
    fidl::endpoints::{create_proxy, create_request_stream},
    fidl_fuchsia_fuzzer::{self as fuzz, Result_ as FuzzResult},
    fidl_fuchsia_logger::MAX_DATAGRAM_LEN_BYTES as BUF_SIZE,
    fuchsia_async as fasync,
    fuchsia_component::client::connect_to_protocol,
    fuchsia_zircon as zx,
    futures::{join, AsyncReadExt, AsyncWriteExt, TryStreamExt},
    serde_json,
    std::cell::RefCell,
    std::rc::Rc,
};

const FUZZER_URL: &str = "fuchsia-pkg://fuchsia.com/fuzz-test-runner-tests#meta/fuzzer.cm";

// Connects to the fuzz-manager, and then connects a controller to the fuzzer.
async fn setup() -> (fuzz::ManagerProxy, fuzz::ControllerProxy, fasync::Task<String>) {
    let fuzz_manager =
        connect_to_protocol::<fuzz::ManagerMarker>().expect("failed to connect fuzz-manager");
    let (controller, log_task) = connect(&fuzz_manager).await;
    let options = fuzz::Options { seed: Some(1), ..fuzz::Options::EMPTY };
    let response = controller.configure(options).await.expect(&controller_name("Configure"));
    assert_eq!(response, zx::Status::OK.into_raw());
    (fuzz_manager, controller, log_task)
}

// Connects a controller to the fuzzer using the fuzz-manager.
async fn connect(
    fuzz_manager: &fuzz::ManagerProxy,
) -> (fuzz::ControllerProxy, fasync::Task<String>) {
    let (controller, server_end) =
        create_proxy::<fuzz::ControllerMarker>().expect("failed to create proxy");
    let status =
        fuzz_manager.connect(FUZZER_URL, server_end).await.expect(&manager_name("Connect"));
    assert_eq!(status, zx::Status::OK.into_raw());

    // Forward syslog. This isn't strictly necessary for testing, but is very useful when debugging.
    let (rx, tx) = zx::Socket::create(zx::SocketOpts::empty()).expect("failed to create sockets");
    let status = fuzz_manager
        .get_output(FUZZER_URL, fuzz::TestOutput::Syslog, tx)
        .await
        .expect(&manager_name("GetOutput"));
    assert_eq!(status, zx::Status::OK.into_raw());
    let log_task = fasync::Task::spawn(async move {
        let mut socket = fasync::Socket::from_socket(rx).unwrap();
        let mut buf: [u8; BUF_SIZE as usize] = [0; BUF_SIZE as usize];
        let mut logs = Vec::new();
        loop {
            let len = socket.read(&mut buf).await.expect("failed to read syslog socket");
            if len == 0 {
                break;
            }
            let line = std::str::from_utf8(&buf[..len]).expect("failed to convert syslog to UTF-8");
            let deserializer = serde_json::Deserializer::from_str(line);
            let iter = deserializer.into_iter::<LogsData>();
            for deserialized in iter {
                let data = match deserialized {
                    Ok(data) => data,
                    Err(_) => continue,
                };
                let msg = data.msg().unwrap_or("<missing>");
                logs.push(format!("[{}] {}", data.metadata.severity, msg));
            }
        }
        logs.join("\n")
    });
    (controller, log_task)
}

// Constructs a `fuzz::Input` that can be sent over FIDL, and the socket to write into it.
fn make_fidl_input(input: &str) -> Result<(fasync::Socket, fuzz::Input)> {
    let (rx, tx) = zx::Socket::create(zx::SocketOpts::STREAM).context("failed to create socket")?;
    let fidl_input = fuzz::Input { socket: rx, size: input.len() as u64 };
    let tx = fasync::Socket::from_socket(tx).context("failed to covert socket")?;
    Ok((tx, fidl_input))
}

// Receives data from the socket in the |input|.
async fn recv_input(input: fuzz::Input) -> Result<Vec<u8>> {
    let mut buf = vec![0; input.size as usize];
    let mut rx =
        fasync::Socket::from_socket(input.socket).context("Failed to create async socket")?;
    rx.read_exact(&mut buf).await.context("Async socket read failed")?;
    Ok(buf)
}

// Simple |fuchsia.fuzzer.CorpusReader| implementation.
async fn serve_corpus_reader(stream: fuzz::CorpusReaderRequestStream) -> Result<Vec<String>> {
    // let corpus: Arc<Mutex<Vec<String>>> = Arc::new(Mutex::new(Vec::new()));
    let corpus = Rc::new(RefCell::new(Vec::new()));
    stream
        .try_for_each(|request| async {
            // let corpus_for_request = corpus.clone();
            match request {
                fuzz::CorpusReaderRequest::Next { test_input, responder } => {
                    let status = match recv_input(test_input).await {
                        Ok(received) => {
                            // let mut corpus = corpus_for_request.lock().await;
                            let as_str = std::str::from_utf8(&received).unwrap().to_string();
                            if !as_str.is_empty() {
                                let mut corpus_mut = corpus.borrow_mut();
                                corpus_mut.push(as_str);
                            }
                            zx::Status::OK
                        }
                        Err(_) => zx::Status::INTERNAL,
                    };
                    responder.send(status.into_raw())
                }
            }
        })
        .await
        .context("CorpusReader failed")?;
    let corpus = corpus.borrow();
    Ok(corpus.clone())
}

// Handles status updates published by the fuzzer.
async fn subscribe_to_updates(
    stream: fuzz::MonitorRequestStream,
) -> Result<Vec<fuzz::UpdateReason>> {
    let reasons = Rc::new(RefCell::new(Vec::new()));
    stream
        .try_for_each(|request| async {
            let result = match request {
                fuzz::MonitorRequest::Update { reason, status: _, responder } => {
                    {
                        let mut reasons = reasons.borrow_mut();
                        reasons.push(reason);
                    }
                    responder.send()
                }
            };
            match result {
                Err(fidl::Error::ServerResponseWrite(s)) if s == zx::Status::PEER_CLOSED => Ok(()),
                other => other,
            }
        })
        .await
        .expect("Monitor error");
    let reasons = reasons.borrow();
    Ok(reasons.clone())
}

// Helper function return a `Ok(_)` or `Err(_)` depending on whether the arguments are equal or not,
// respectively.
fn compare<T: PartialEq>(x: &T, y: T) -> Result<()> {
    if x.ne(&y) {
        bail!("not equal");
    }
    Ok(())
}

// Returns the name of the fuzz-manager FIDL method.
fn manager_name(method: &str) -> String {
    format!("fuchsia.fuzzer.Manager/{}", method)
}

// Returns the name of the fuzzer controller FIDL method.
fn controller_name(method: &str) -> String {
    format!("fuchsia.fuzzer.Controller/{}", method)
}

// Stops the fuzzer and dumps the log if the test resulted in an error.
async fn teardown(
    fuzz_manager: fuzz::ManagerProxy,
    test_result: Result<()>,
    log_task: fasync::Task<String>,
) -> Result<()> {
    let stop_result = stop(&fuzz_manager).await.map_err(Error::msg);
    if let Err(e) = test_result.and(stop_result) {
        let log = log_task.await;
        eprintln!("{}", log);
        return Err(e);
    }
    Ok(())
}

// Stops the fuzzer.
async fn stop(fuzz_manager: &fuzz::ManagerProxy) -> Result<(), zx::Status> {
    zx::Status::ok(fuzz_manager.stop(FUZZER_URL).await.expect(&manager_name("Stop")))
}

#[fuchsia::test]
async fn test_reconnect() -> Result<()> {
    let fuzz_manager =
        connect_to_protocol::<fuzz::ManagerMarker>().expect("failed to connect to fuzz-manager");

    // Set an option.
    let (controller1, log_task1) = connect(&fuzz_manager).await;
    let test1 = || async move {
        let options = fuzz::Options { dictionary_level: Some(13), ..fuzz::Options::EMPTY };
        let response =
            controller1.configure(options).await.context(controller_name("Configure"))?;
        zx::Status::ok(response).map_err(Error::msg)
    };
    let test_result1 = test1().await;

    // Now reconnect and use the option to verify the instance is the same.
    let (controller2, log_task2) = connect(&fuzz_manager).await;
    let test2 = || async move {
        let options = controller2.get_options().await.context(controller_name("GetOptions"))?;
        compare(&options.dictionary_level, Some(13))
    };
    let test_result2 = test2().await;

    // Stop the fuzzer and join the logs.
    let stop_result = stop(&fuzz_manager).await.map_err(Error::msg);
    if let Err(e) = test_result1.and(test_result2).and(stop_result) {
        let (log1, log2) = join!(log_task1, log_task2);
        // let log2 = log_task2.await;
        eprintln!("{}", log1);
        eprintln!("{}", log2);
        return Err(e);
    }
    Ok(())
}

#[fuchsia::test]
async fn test_stop() -> Result<()> {
    let (fuzz_manager, _controller, log_task) = setup().await;

    // Stop.
    if let Err(e) = stop(&fuzz_manager).await.map_err(Error::msg) {
        let log = log_task.await;
        eprintln!("{}", log);
        return Err(e);
    }

    // Stop when already stopped.
    let response = stop(&fuzz_manager).await;
    assert_eq!(response, Err(zx::Status::NOT_FOUND));
    Ok(())
}

#[fuchsia::test]
async fn test_configure() -> Result<()> {
    let (fuzz_manager, controller, log_task) = setup().await;
    let test = || async move {
        let options = fuzz::Options { max_input_size: Some(1024), ..fuzz::Options::EMPTY };
        let response = controller.configure(options).await.context(controller_name("Configure"))?;
        compare(&response, zx::Status::OK.into_raw())?;

        let options = controller.get_options().await.context(controller_name("GetOptions"))?;
        compare(&options.max_input_size, Some(1024))
    };
    teardown(fuzz_manager, test().await, log_task).await
}

#[fuchsia::test]
async fn test_execute_no_errors() -> Result<()> {
    let (fuzz_manager, controller, log_task) = setup().await;
    let test = || async move {
        let input = "NoErrors";
        let (mut tx, mut fidl_input) =
            make_fidl_input(input).context("failed to make FIDL input")?;
        let results = join!(tx.write_all(input.as_bytes()), controller.execute(&mut fidl_input));
        results.0.context("failed to send input")?;
        let response = results.1.context(controller_name("Execute"))?;
        let result = response.map_err(|e| zx::Status::from_raw(e)).context("received error")?;
        compare(&result, FuzzResult::NoErrors)
    };
    teardown(fuzz_manager, test().await, log_task).await
}

#[fuchsia::test]
async fn test_execute_crash() -> Result<()> {
    let (fuzz_manager, controller, log_task) = setup().await;
    let test = || async move {
        let input = "CRASH";
        let (mut tx, mut fidl_input) =
            make_fidl_input(input).context("failed to make FIDL input")?;
        let results = join!(tx.write_all(input.as_bytes()), controller.execute(&mut fidl_input));
        results.0.context("failed to send input")?;
        let response = results.1.context(controller_name("Execute"))?;
        let result = response.map_err(|e| zx::Status::from_raw(e)).context("received error")?;
        compare(&result, FuzzResult::Crash)
    };
    teardown(fuzz_manager, test().await, log_task).await
}

#[fuchsia::test]
async fn test_fuzz_until_crash() -> Result<()> {
    let (fuzz_manager, controller, log_task) = setup().await;
    let test = || async move {
        let response = controller.fuzz().await.context(controller_name("Fuzz"))?;
        let fuzzed = response.map_err(|e| zx::Status::from_raw(e)).context("received error")?;
        compare(&fuzzed.0, FuzzResult::Crash)?;
        let error_input = recv_input(fuzzed.1).await?;
        compare(&error_input, b"CRASH".to_vec())
    };
    teardown(fuzz_manager, test().await, log_task).await
}

#[fuchsia::test]
async fn test_fuzz_until_runs() -> Result<()> {
    let (fuzz_manager, controller, log_task) = setup().await;
    let test = || async move {
        let options = fuzz::Options {
            seed: Some(1),
            runs: Some(100),
            max_input_size: Some(4),
            ..fuzz::Options::EMPTY
        };
        let response = controller.configure(options).await.context(controller_name("Configure"))?;
        compare(&response, zx::Status::OK.into_raw())?;
        let (client_end, stream) = create_request_stream::<fuzz::MonitorMarker>()?;
        controller.add_monitor(client_end).await.context(controller_name("AddMonitor"))?;
        let results = join!(controller.fuzz(), subscribe_to_updates(stream));
        let response = results.0.context(controller_name("Fuzz"))?;
        let fuzzed = response.map_err(|e| zx::Status::from_raw(e)).context("received error")?;
        compare(&fuzzed.0, FuzzResult::NoErrors)?;
        let reasons = results.1.context("failed to get updates")?;
        compare(&reasons.first(), Some(&fuzz::UpdateReason::Init))?;
        compare(&reasons.last(), Some(&fuzz::UpdateReason::Done))
    };
    teardown(fuzz_manager, test().await, log_task).await
}

#[fuchsia::test]
async fn test_minimize() -> Result<()> {
    let (fuzz_manager, controller, log_task) = setup().await;
    let test = || async move {
        let input = "THIS CRASH CAN BE MINIMIZED";
        let (mut tx, mut fidl_input) =
            make_fidl_input(input).context("failed to make FIDL input")?;
        let results = join!(tx.write_all(input.as_bytes()), controller.minimize(&mut fidl_input));
        results.0.context("failed to send input")?;
        let response = results.1.context("FIDL failure")?;
        let minimized = response.map_err(|e| zx::Status::from_raw(e)).context("received error")?;
        let error_input = recv_input(minimized).await?;
        compare(&error_input, b"CRASH".to_vec())
    };
    teardown(fuzz_manager, test().await, log_task).await
}

#[fuchsia::test]
async fn test_cleanse() -> Result<()> {
    let (fuzz_manager, controller, log_task) = setup().await;
    let test = || async move {
        let input = "AAAACRASHAAAA";
        let (mut tx, mut fidl_input) =
            make_fidl_input(input).context("failed to make FIDL input")?;
        let results = join!(tx.write_all(input.as_bytes()), controller.cleanse(&mut fidl_input));
        results.0.context("failed to send input")?;
        let response = results.1.context("FIDL failure")?;
        let cleansed = response.map_err(|e| zx::Status::from_raw(e)).context("received error")?;
        let error_input = recv_input(cleansed).await?;
        compare(&error_input, b"    CRASH    ".to_vec())
    };
    teardown(fuzz_manager, test().await, log_task).await
}

#[fuchsia::test]
async fn test_merge() -> Result<()> {
    let (fuzz_manager, controller, log_task) = setup().await;
    let test = || async move {
        let seed_inputs = vec!["C", "AS"];
        for input in seed_inputs.iter() {
            let (mut tx, mut fidl_input) =
                make_fidl_input(input).context("failed to make FIDL input")?;
            let results = join!(
                tx.write_all(input.as_bytes()),
                controller.add_to_corpus(fuzz::Corpus::Seed, &mut fidl_input)
            );
            results.0.context("failed to send input")?;
            let result = results.1.context(controller_name("AddToCorpus"))?;
            zx::Status::ok(result).context("received error")?;
        }

        let live_inputs = vec!["CR", "CRY", "CASH", "CRASS", "CRAM", "CRAMS", "SCRAM"];
        for input in live_inputs {
            let (mut tx, mut fidl_input) =
                make_fidl_input(input).context("failed to make FIDL input")?;
            let results = join!(
                tx.write_all(input.as_bytes()),
                controller.add_to_corpus(fuzz::Corpus::Live, &mut fidl_input)
            );
            results.0.context("failed to send input")?;
            let result = results.1.context(controller_name("AddToCorpus"))?;
            zx::Status::ok(result).context("received error")?;
        }

        let response = controller.merge().await.context(controller_name("Merge"))?;
        compare(&response, zx::Status::OK.into_raw())?;

        // Get the seed corpus.
        let (client_end, stream) = create_request_stream::<fuzz::CorpusReaderMarker>()?;
        let results = join!(
            controller.read_corpus(fuzz::Corpus::Seed, client_end),
            serve_corpus_reader(stream),
        );
        results.0.context(controller_name("ReadCorpus (seed)"))?;
        let actual_seed = results.1.context("failed to serve seed corpus reader")?;
        compare(&actual_seed, seed_inputs.iter().map(|s| s.to_string()).collect())?;

        // Get the live corpus.
        let (client_end, stream) = create_request_stream::<fuzz::CorpusReaderMarker>()?;
        let results = join!(
            controller.read_corpus(fuzz::Corpus::Live, client_end),
            serve_corpus_reader(stream),
        );
        results.0.context(controller_name("ReadCorpus (live)"))?;
        let actual_live = results.1.context("failed to serve live corpus reader")?;

        // The fake runner "measures" inputs by setting the number of features to matching prefix
        // length. The merge algorithm sorts inputs by shortest first, then most features, and it keeps
        // any inputs that add new features.
        compare(&actual_live, vec!["CR", "CRAM", "CRASS"].iter().map(|s| s.to_string()).collect())
    };
    teardown(fuzz_manager, test().await, log_task).await
}
