// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::diagnostics::send_log_entry,
    crate::options::add_defaults,
    crate::test::Test,
    anyhow::{anyhow, Context as _, Result},
    fidl_fuchsia_fuzzer::{self as fuzz, Input as FidlInput, Result_ as FuzzResult},
    fuchsia_async as fasync,
    fuchsia_fuzzctl::InputPair,
    fuchsia_zircon_status as zx,
    futures::{join, AsyncReadExt, AsyncWriteExt, StreamExt},
    std::cell::RefCell,
    std::rc::Rc,
};

/// Test fake that allows configuring how to respond to `fuchsia.fuzzer.Controller` methods.
///
/// These fields are Rc<RefCell<_>> in order to be cloned and shared with the `Task` serving the
/// controller. Unit tests can use this object to query values passed in FIDL requests and set
/// values returned by FIDL responses.
#[derive(Debug)]
pub struct FakeController {
    corpus_type: Rc<RefCell<fuzz::Corpus>>,
    input_to_send: Rc<RefCell<Option<Vec<u8>>>>,
    options: Rc<RefCell<fuzz::Options>>,
    received_input: Rc<RefCell<Vec<u8>>>,
    result: Rc<RefCell<Result<FuzzResult, zx::Status>>>,
    status: Rc<RefCell<fuzz::Status>>,
    stdout: Rc<RefCell<Option<fasync::Socket>>>,
    stderr: Rc<RefCell<Option<fasync::Socket>>>,
    syslog: Rc<RefCell<Option<fasync::Socket>>>,
    canceled: Rc<RefCell<bool>>,
}

impl FakeController {
    /// Creates a fake fuzzer that can serve `fuchsia.fuzzer.Controller`.
    pub fn new() -> Self {
        let status = fuzz::Status { running: Some(false), ..fuzz::Status::EMPTY };
        let mut options = fuzz::Options::EMPTY;
        add_defaults(&mut options);
        Self {
            corpus_type: Rc::new(RefCell::new(fuzz::Corpus::Seed)),
            input_to_send: Rc::new(RefCell::new(None)),
            options: Rc::new(RefCell::new(options)),
            received_input: Rc::new(RefCell::new(Vec::new())),
            result: Rc::new(RefCell::new(Ok(FuzzResult::NoErrors))),
            status: Rc::new(RefCell::new(status)),
            stdout: Rc::new(RefCell::new(None)),
            stderr: Rc::new(RefCell::new(None)),
            syslog: Rc::new(RefCell::new(None)),
            canceled: Rc::new(RefCell::new(false)),
        }
    }

    /// Simulates a call to `fuchsia.fuzzer.Manager/GetOutput` without a `fuzz-manager`.
    pub fn set_output(&self, output: fuzz::TestOutput, socket: fidl::Socket) -> zx::Status {
        let socket = fasync::Socket::from_socket(socket).expect("failed to create sockets");
        match output {
            fuzz::TestOutput::Stdout => {
                let mut stdout_mut = self.stdout.borrow_mut();
                *stdout_mut = Some(socket);
            }
            fuzz::TestOutput::Stderr => {
                let mut stderr_mut = self.stderr.borrow_mut();
                *stderr_mut = Some(socket);
            }
            fuzz::TestOutput::Syslog => {
                let mut syslog_mut = self.syslog.borrow_mut();
                *syslog_mut = Some(socket);
            }
            _ => todo!("not supported"),
        }
        zx::Status::OK
    }

    /// Returns the type of corpus received via FIDL requests.
    pub fn get_corpus_type(&self) -> fuzz::Corpus {
        self.corpus_type.borrow().clone()
    }

    /// Sets the type of corpus to return via FIDL responses.
    pub fn set_corpus_type(&self, corpus_type: fuzz::Corpus) {
        let mut corpus_type_mut = self.corpus_type.borrow_mut();
        *corpus_type_mut = corpus_type;
    }

    /// Returns the test input to be sent via a FIDL response.
    pub fn take_input_to_send(&self) -> Option<Vec<u8>> {
        self.input_to_send.borrow_mut().take()
    }

    /// Sets the test input to be sent via a FIDL response.
    pub fn set_input_to_send(&self, input_to_send: &[u8]) {
        let mut input_to_send_mut = self.input_to_send.borrow_mut();
        *input_to_send_mut = Some(input_to_send.to_vec());
    }

    /// Returns the options received via FIDL requests.
    pub fn get_options(&self) -> fuzz::Options {
        self.options.borrow().clone()
    }

    /// Sets the options to return via FIDL responses.
    pub fn set_options(&self, mut options: fuzz::Options) {
        add_defaults(&mut options);
        let mut options_mut = self.options.borrow_mut();
        *options_mut = options;
    }

    /// Returns the test input received via FIDL requests.
    pub fn get_received_input(&self) -> Vec<u8> {
        self.received_input.borrow().clone()
    }

    /// Reads test input data from a `fuchsia.fuzzer.Input` from a FIDL request.
    async fn receive_input(&self, input: FidlInput) -> Result<()> {
        let mut received_input = Vec::new();
        let mut reader = fidl::AsyncSocket::from_socket(input.socket)?;
        reader.read_to_end(&mut received_input).await?;
        let mut received_input_mut = self.received_input.borrow_mut();
        *received_input_mut = received_input;
        Ok(())
    }

    /// Returns the fuzzing result to be sent via a FIDL response.
    pub fn get_result(&self) -> Result<FuzzResult, zx::Status> {
        self.result.borrow().clone()
    }

    /// Sets the fuzzing result to be sent via a FIDL response.
    pub fn set_result(&self, result: Result<FuzzResult, zx::Status>) {
        let mut result_mut = self.result.borrow_mut();
        *result_mut = result;
    }

    /// Gets the fuzzer status to be sent via FIDL responses.
    pub fn get_status(&self) -> fuzz::Status {
        self.status.borrow().clone()
    }

    /// Sets the fuzzer status to be sent via FIDL responses.
    pub fn set_status(&self, status: fuzz::Status) {
        let mut status_mut = self.status.borrow_mut();
        *status_mut = status;
    }

    // Simulates sending a `msg` to a fuzzer's standard output, standard error, and system log.
    async fn send_output(&self, msg: &str) -> Result<()> {
        let msg_str = format!("{}\n", msg);
        {
            let mut stdout_mut = self.stdout.borrow_mut();
            if let Some(mut stdout) = stdout_mut.take() {
                stdout.write_all(msg_str.as_bytes()).await?;
                *stdout_mut = Some(stdout);
            }
        }
        {
            let mut stderr_mut = self.stderr.borrow_mut();
            if let Some(mut stderr) = stderr_mut.take() {
                stderr.write_all(msg_str.as_bytes()).await?;
                *stderr_mut = Some(stderr);
            }
        }
        {
            let mut syslog_mut = self.syslog.borrow_mut();
            if let Some(mut syslog) = syslog_mut.take() {
                send_log_entry(&mut syslog, msg).await?;
                *syslog_mut = Some(syslog);
            }
        }
        Ok(())
    }

    /// Simulates a long-running workflow being canceled by `fuchsia.fuzzer.Manager/Stop`.
    pub fn cancel(&self) {
        let mut canceled_mut = self.canceled.borrow_mut();
        *canceled_mut = true;
    }

    /// Get whether a simulated call to `fuchsia.fuzzer.Manager/Stop` has been made.
    pub fn is_canceled(&self) -> bool {
        *self.canceled.borrow()
    }
}

impl Clone for FakeController {
    fn clone(&self) -> Self {
        Self {
            corpus_type: Rc::clone(&self.corpus_type),
            input_to_send: Rc::clone(&self.input_to_send),
            options: Rc::clone(&self.options),
            received_input: Rc::clone(&self.received_input),
            result: Rc::clone(&self.result),
            status: Rc::clone(&self.status),
            stdout: Rc::clone(&self.stdout),
            stderr: Rc::clone(&self.stderr),
            syslog: Rc::clone(&self.syslog),
            canceled: Rc::clone(&self.canceled),
        }
    }
}

/// Serves `fuchsia.fuzzer.Controller` using test fakes.
pub async fn serve_controller(
    mut stream: fuzz::ControllerRequestStream,
    mut test: Test,
) -> Result<()> {
    let mut _responder = None;
    let fake = test.controller();
    loop {
        let request = stream.next().await;
        if fake.is_canceled() {
            break;
        }
        match request {
            Some(Ok(fuzz::ControllerRequest::Configure { options, responder })) => {
                test.record("fuchsia.fuzzer.Controller/Configure");
                fake.set_options(options);
                responder.send(zx::Status::OK.into_raw())?;
            }
            Some(Ok(fuzz::ControllerRequest::GetOptions { responder })) => {
                test.record("fuchsia.fuzzer.Controller/GetOptions");
                let options = fake.get_options();
                responder.send(options)?;
            }
            Some(Ok(fuzz::ControllerRequest::AddToCorpus { corpus, input, responder })) => {
                test.record(format!("fuchsia.fuzzer.Controller/AddToCorpus({:?})", corpus));
                fake.receive_input(input).await?;
                fake.set_corpus_type(corpus);
                responder.send(zx::Status::OK.into_raw())?;
            }
            Some(Ok(fuzz::ControllerRequest::ReadCorpus { corpus, corpus_reader, responder })) => {
                test.record("fuchsia.fuzzer.Controller/ReadCorpus");
                fake.set_corpus_type(corpus);
                let corpus_reader = corpus_reader.into_proxy()?;
                if let Some(input_to_send) = fake.take_input_to_send() {
                    let input_pair = InputPair::try_from_data(input_to_send)?;
                    let (mut fidl_input, input) = input_pair.as_tuple();
                    let corpus_fut = corpus_reader.next(&mut fidl_input);
                    let input_fut = input.send();
                    let results = join!(corpus_fut, input_fut);
                    assert!(results.0.is_ok());
                    assert!(results.1.is_ok());
                }
                responder.send()?;
            }
            Some(Ok(fuzz::ControllerRequest::GetStatus { responder })) => {
                test.record("fuchsia.fuzzer.Controller/GetStatus");
                let status = fake.get_status();
                responder.send(status)?;
            }
            Some(Ok(fuzz::ControllerRequest::Execute { test_input, responder })) => {
                test.record("fuchsia.fuzzer.Controller/Execute");
                fake.receive_input(test_input).await?;
                match fake.get_result() {
                    Ok(fuzz_result) => {
                        responder.send(&mut Ok(fuzz_result))?;
                        fake.send_output(fuzz::DONE_MARKER).await?;
                    }
                    Err(status) => {
                        responder.send(&mut Err(status.into_raw()))?;
                    }
                }
            }
            Some(Ok(fuzz::ControllerRequest::Minimize { test_input, responder })) => {
                test.record("fuchsia.fuzzer.Controller/Minimize");
                fake.receive_input(test_input).await?;
                let input_to_send = fake.take_input_to_send().context("input_to_send unset")?;
                let input_pair = InputPair::try_from_data(input_to_send)?;
                let (fidl_input, input) = input_pair.as_tuple();
                fake.set_result(Ok(FuzzResult::Minimized));
                responder.send(&mut Ok(fidl_input))?;
                input.send().await?;
                fake.send_output(fuzz::DONE_MARKER).await?;
            }
            Some(Ok(fuzz::ControllerRequest::Cleanse { test_input, responder })) => {
                test.record("fuchsia.fuzzer.Controller/Cleanse");
                fake.receive_input(test_input).await?;
                let input_to_send = fake.take_input_to_send().context("input_to_send unset")?;
                let input_pair = InputPair::try_from_data(input_to_send)?;
                let (fidl_input, input) = input_pair.as_tuple();
                fake.set_result(Ok(FuzzResult::Cleansed));
                responder.send(&mut Ok(fidl_input))?;
                input.send().await?;
                fake.send_output(fuzz::DONE_MARKER).await?;
            }
            Some(Ok(fuzz::ControllerRequest::Fuzz { responder })) => {
                test.record("fuchsia.fuzzer.Controller/Fuzz");
                // As special cases, fuzzing indefinitely without any errors or fuzzing with an
                // explicit error of `SHOULD_WAIT` will imitate a FIDL call that does not
                // complete. These can be interrupted by the shell or allowed to timeout.
                let result = fake.get_result();
                let options = fake.get_options();
                match (options.runs, options.max_total_time, result) {
                    (Some(0), Some(0), Ok(FuzzResult::NoErrors))
                    | (_, _, Err(zx::Status::SHOULD_WAIT)) => {
                        let mut status = fake.get_status();
                        status.running = Some(true);
                        fake.set_status(status);
                        // Prevent the responder being dropped and closing the stream.
                        _responder = Some(responder);
                    }
                    (_, _, Ok(fuzz_result)) => {
                        let input_to_send = fake.take_input_to_send().unwrap_or(Vec::new());
                        let input_pair = InputPair::try_from_data(input_to_send)?;
                        let (fidl_input, input) = input_pair.as_tuple();
                        let mut response = Ok((fuzz_result, fidl_input));
                        responder.send(&mut response)?;
                        input.send().await?;
                        fake.send_output(fuzz::DONE_MARKER).await?;
                    }
                    (_, _, Err(status)) => {
                        responder.send(&mut Err(status.into_raw()))?;
                    }
                };
            }
            Some(Ok(fuzz::ControllerRequest::Merge { responder })) => {
                test.record("fuchsia.fuzzer.Controller/Merge");
                fake.set_result(Ok(FuzzResult::Merged));
                responder.send(zx::Status::OK.into_raw())?;
                fake.send_output(fuzz::DONE_MARKER).await?;
            }
            Some(Err(e)) => return Err(anyhow!(e)),
            None => break,
            _ => todo!("not yet implemented"),
        };
    }
    Ok(())
}
