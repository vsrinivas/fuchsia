// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/**
 * This program integration-tests the triage-detect program using the OpaqueTest library
 * to inject a fake CrashReporter, ArchiveAccessor, and config-file directory.
 *
 * triage-detect will be able to fetch Diagnostic data, evaluate it according to the .triage
 * files it finds, and request whatever crash reports are appropriate. Meanwhile, the fakes
 * will be writing TestEvent entries to a stream for later evaluation.
 *
 * Each integration test is stored in a file whose name starts with "test". This supplies:
 * 1) Some number of Diagnostic data strings. When the program tries to fetch Diagnostic data
 *  after these strings are exhausted, the fake ArchiveAccessor writes to a special "done" channel
 *  to terminate the test.
 * 2) Some number of config files to place in the fake directory.
 * 3) A vector of vectors of crash report signatures. The inner vector should match the crash
 *  report requests sent between each fetch of Diagnostic data. Order of the inner vector does
 *  not matter, but duplicates do matter.
 */
mod fake_archive_accessor;
mod fake_crash_reporter;

use {
    anyhow::{bail, Error},
    fake_archive_accessor::FakeArchiveAccessor,
    fake_crash_reporter::FakeCrashReporter,
    fuchsia_async as fasync,
    futures::{channel::mpsc, SinkExt, StreamExt},
    log::*,
    std::sync::Arc,
    test_utils_lib::{
        injectors::{CapabilityInjector, DirectoryInjector},
        matcher::EventMatcher,
        opaque_test::OpaqueTest,
    },
    vfs::{
        directory::{
            helper::DirectlyMutable, immutable::connection::io1::ImmutableConnection,
            simple::Simple,
        },
        file::pcb::asynchronous::read_only_const,
        pseudo_directory,
    },
};

const DETECT_PROGRAM_URL: &str =
    "fuchsia-pkg://fuchsia.com/detect-integration-test#meta/detect-component.cm";
// Keep this the same as the command line arg in meta/detect.cml.
const CHECK_PERIOD_SECONDS: u64 = 5;

// Test that the "repeat" field of snapshots works correctly.
mod test_snapshot_throttle;
// Test that the "trigger" field of snapshots works correctly.
mod test_trigger_truth;
// run_a_test() verifies the Diagnostic-reading period by checking that no test completes too early.
// Manually verified: test fails if command line is 4 seconds and CHECK_PERIOD_SECONDS is 5.

async fn run_all_tests() -> Vec<Result<(), Error>> {
    futures::future::join_all(vec![
        run_a_test(test_snapshot_throttle::test()),
        run_a_test(test_trigger_truth::test()),
    ])
    .await
}

#[fasync::run_singlethreaded(test)]
async fn entry_point() -> Result<(), Error> {
    fuchsia_syslog::init().unwrap();
    fuchsia_syslog::set_severity(fuchsia_syslog::levels::DEBUG);

    for result in run_all_tests().await.into_iter() {
        if result.is_err() {
            error!("{:?}", result);
            return result;
        }
    }
    Ok(())
}

// Each test*.rs file returns one of these from its "pub test()" method.
pub struct TestData {
    name: String,
    config_files: Vec<ConfigFile>,
    #[allow(dead_code)]
    inspect_data: Vec<String>,
    crashes: Vec<Vec<String>>,
}

// ConfigFile contains file information to help build a PseudoDirectory of configuration files.
struct ConfigFile {
    name: String,
    contents: String,
}

// These are written to the reporting stream by the injected protocol fakes.
#[derive(Debug)]
pub enum TestEvent {
    CrashReport(String),
    DiagnosticFetch,
}

type TestEventSender = mpsc::UnboundedSender<Result<TestEvent, Error>>;
type TestEventReceiver = mpsc::UnboundedReceiver<Result<TestEvent, Error>>;
type DoneSender = mpsc::Sender<()>;
type DoneReceiver = mpsc::Receiver<()>;

#[derive(Clone, Debug)]
pub struct DoneSignaler {
    sender: DoneSender,
}

impl DoneSignaler {
    async fn signal_done(&self) {
        self.sender.clone().send(()).await.unwrap();
    }
}

#[derive(Debug)]
pub struct DoneWaiter {
    sender: DoneSender,
    receiver: DoneReceiver,
}

impl DoneWaiter {
    fn new() -> DoneWaiter {
        let (sender, receiver) = mpsc::channel(0);
        DoneWaiter { sender, receiver }
    }

    fn get_signaler(&self) -> DoneSignaler {
        DoneSignaler { sender: self.sender.clone() }
    }

    async fn wait(&mut self) {
        self.receiver.next().await;
    }
}

async fn run_a_test(test_data: TestData) -> Result<(), Error> {
    info!("Running test {}", test_data.name);
    let start_time = std::time::Instant::now();
    let mut done_waiter = DoneWaiter::new();
    let (events_sender, events_receiver) = mpsc::unbounded();

    // Start a component_manager as a v1 component
    let test = OpaqueTest::default(DETECT_PROGRAM_URL).await.unwrap();
    let event_source = test.connect_to_event_source().await.unwrap();

    DirectoryInjector::new(prepare_injected_config_directory(&test_data))
        .inject(&event_source, EventMatcher::ok().capability_id("config-data"))
        .await;

    let capability =
        FakeArchiveAccessor::new(&test_data, events_sender.clone(), done_waiter.get_signaler());
    capability.inject(&event_source, EventMatcher::ok()).await;

    let capability = FakeCrashReporter::new(events_sender.clone(), done_waiter.get_signaler());
    capability.inject(&event_source, EventMatcher::ok()).await;

    // Unblock the component_manager.
    event_source.start_component_tree().await;

    // Await the test result.
    done_waiter.wait().await;
    let events = drain(events_receiver).await?;
    let end_time = std::time::Instant::now();
    let minimum_test_time = CHECK_PERIOD_SECONDS * (test_data.inspect_data.len() as u64);
    assert!(end_time - start_time >= std::time::Duration::new(minimum_test_time, 0));
    assert!(evaluate_test_results(&test_data, &events).is_ok());
    Ok(())
}

// Some of the senders of the Event stream will remain open, so we can't wait for it to be
// conventionally "done." After the Done stream has been written to, whatever events
// are in the event stream constitute the test result. Read them out using poll and
// return them.
async fn drain(mut stream: TestEventReceiver) -> Result<Vec<TestEvent>, Error> {
    let mut result = Vec::new();
    loop {
        match stream.try_next() {
            Ok(Some(item)) => result.push(item?),
            Ok(None) => unreachable!(),
            Err(_) => return Ok(result),
        }
    }
}

fn evaluate_test_results(test: &TestData, events: &Vec<TestEvent>) -> Result<(), Error> {
    let mut crash_list = Vec::new();
    let mut crashes_list = Vec::new();
    let mut events = events.iter();
    info!("TestCrashes: {:?}", test.crashes);
    info!("events: {:?}", events);
    match events.next() {
        Some(TestEvent::DiagnosticFetch) => {}
        Some(TestEvent::CrashReport(r)) => {
            bail!("{}: First event was a crash report: {}", test.name, r)
        }
        None => bail!("{}: Events were empty", test.name),
    }
    // If all goes well, the last event should be a DiagnosticFetch (which the tester never
    // replies to). In the loop below, this will cause the previous fetch's crash_list to be
    // added to crashes_list. The final crash_list created should remain empty.)
    for event in events {
        match event {
            TestEvent::DiagnosticFetch => {
                crashes_list.push(crash_list);
                crash_list = Vec::new();
            }
            TestEvent::CrashReport(signature) => crash_list.push(signature.to_string()),
        }
    }
    if crash_list.len() != 0 {
        bail!("{}: Crashes were filed after the final fetch: {:?}", test.name, crash_list);
    }
    let mut desired_events = test.crashes.iter();
    let mut actual_events = crashes_list.iter();
    let mut which_iteration = 0;
    info!("crashes_list: {:?}", crashes_list);
    loop {
        match (desired_events.next(), actual_events.next()) {
            (None, None) => break,
            (Some(desired), Some(actual)) => {
                let mut desired = desired.clone();
                let mut actual = actual.clone();
                desired.sort_unstable();
                actual.sort_unstable();
                if desired != actual {
                    bail!(
                        "{}: Step {}, desired {:?}, actual {:?}",
                        test.name,
                        which_iteration,
                        desired,
                        actual
                    );
                }
            }
            (desired, actual) => {
                bail!(
                    "{}: Step {}, desired {:?}, actual {:?}",
                    test.name,
                    which_iteration,
                    desired,
                    actual
                );
            }
        }
        which_iteration += 1;
    }
    Ok(())
}

fn prepare_injected_config_directory(test: &TestData) -> Arc<Simple<ImmutableConnection>> {
    let leaf;
    let data_directory = pseudo_directory! {
        "data" => pseudo_directory! {leaf -> },
    };
    for ConfigFile { name, contents } in test.config_files.iter() {
        leaf.add_entry(name, read_only_const(contents.to_string().into_bytes())).unwrap();
    }
    data_directory
}
