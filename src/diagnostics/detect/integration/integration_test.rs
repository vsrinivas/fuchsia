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
mod fake_crash_reporter;
mod fake_crash_reporting_product_register;

use {
    anyhow::{bail, format_err, Error},
    async_trait::async_trait,
    component_events::{events::*, matcher::*},
    fake_archive_accessor::FakeArchiveAccessor,
    fake_crash_reporter::FakeCrashReporter,
    fake_crash_reporting_product_register::FakeCrashReportingProductRegister,
    fidl_fuchsia_diagnostics as diagnostics, fidl_fuchsia_feedback as fcrash,
    fidl_fuchsia_io as fio,
    fuchsia_component::server::*,
    fuchsia_component_test::{
        Capability, ChildOptions, LocalComponentHandles, RealmBuilder, Ref, Route,
    },
    fuchsia_zircon as zx,
    futures::{channel::mpsc, future::BoxFuture, FutureExt, SinkExt, StreamExt},
    std::sync::Arc,
    tracing::*,
};

const DETECT_PROGRAM_URL: &str =
    "fuchsia-pkg://fuchsia.com/detect-integration-test#meta/triage-detect.cm";
// Keep this the same as the command line arg in meta/detect.cml.
const CHECK_PERIOD_SECONDS: u64 = 5;

// Test that the "repeat" field of snapshots works correctly.
mod test_snapshot_throttle;
// Test that the "trigger" field of snapshots works correctly.
mod test_trigger_truth;
// Test that no report is filed unless "config.json" has the magic contents.
mod test_filing_enable;
// Test that all characters other than [a-z-] are converted to that set.
mod test_snapshot_sanitizing;

#[fuchsia::test]
async fn test_snapshot_throttle() -> Result<(), Error> {
    run_a_test(test_snapshot_throttle::test()).await
}

#[fuchsia::test]
async fn test_trigger_truth() -> Result<(), Error> {
    run_a_test(test_trigger_truth::test()).await
}
#[fuchsia::test]
async fn test_snapshot_sanitizing() -> Result<(), Error> {
    run_a_test(test_snapshot_sanitizing::test()).await
}

#[fuchsia::test]
async fn test_filing_enable() -> Result<(), Error> {
    run_a_test(test_filing_enable::test_with_enable()).await
}

#[fuchsia::test]
async fn test_filing_bad_enable() -> Result<(), Error> {
    run_a_test(test_filing_enable::test_bad_enable()).await
}

#[fuchsia::test]
async fn test_filing_false_enable() -> Result<(), Error> {
    run_a_test(test_filing_enable::test_false_enable()).await
}

#[fuchsia::test]
async fn test_filing_no_enable() -> Result<(), Error> {
    run_a_test(test_filing_enable::test_no_enable()).await
}

#[fuchsia::test]
async fn test_filing_without_file() -> Result<(), Error> {
    run_a_test(test_filing_enable::test_without_file()).await
}

// Each test*.rs file returns one of these from its "pub test()" method.
#[derive(Clone)]
pub struct TestData {
    // Just used for logging.
    name: String,
    // Contents of the /config/data directory. May include config.json.
    config_files: Vec<ConfigFile>,
    // Inspect-json-format strings, to be returned each time Detect fetches Inspect data.
    inspect_data: Vec<String>,
    // Between each fetch, the program should file these crash reports. For the inner vec,
    // the order doesn't matter but duplicates will be retained and checked (it's not a set).
    snapshots: Vec<Vec<String>>,
    // Does the Detect program quit without fetching Inspect data?
    bails: bool,
}

// ConfigFile contains file information to help build a PseudoDirectory of configuration files.
#[derive(Clone)]
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

struct ArchiveEventSignaler {
    event_sender: TestEventSender,
    done_signaler: DoneSignaler,
}

impl ArchiveEventSignaler {
    fn new(event_sender: TestEventSender, done_signaler: DoneSignaler) -> ArchiveEventSignaler {
        ArchiveEventSignaler { event_sender, done_signaler }
    }
}

#[async_trait]
impl fake_archive_accessor::EventSignaler for ArchiveEventSignaler {
    async fn signal_fetch(&self) {
        self.event_sender.clone().send(Ok(TestEvent::DiagnosticFetch)).await.unwrap();
    }
    async fn signal_done(&self) {
        self.done_signaler.signal_done().await;
    }
    async fn signal_error(&self, error: &str) {
        self.event_sender.clone().send(Err(format_err!("{}", error))).await.unwrap();
    }
}

fn create_mock_component(
    test_data: TestData,
    crash_reporter: Arc<FakeCrashReporter>,
    crash_reporting_product_register: Arc<FakeCrashReportingProductRegister>,
    archive_accessor: Arc<FakeArchiveAccessor>,
) -> impl Fn(LocalComponentHandles) -> BoxFuture<'static, Result<(), anyhow::Error>>
       + Sync
       + Send
       + 'static {
    move |mock_handles| {
        let test_data = test_data.clone();
        let crash_reporter = crash_reporter.clone();
        let crash_reporting_product_register = crash_reporting_product_register.clone();
        let archive_accessor = archive_accessor.clone();

        async move {
            let _ = &mock_handles;
            let mut fs = ServiceFs::new();

            // Serve data directory
            let mut config_dir = fs.dir("config");
            let mut data_dir = config_dir.dir("data");

            for ConfigFile { name, contents } in test_data.config_files.iter() {
                let vmo = zx::Vmo::create(contents.len() as u64).unwrap();
                vmo.write(contents.as_bytes(), 0).unwrap();
                data_dir.add_vmo_file_at(name, vmo);
            }

            // Serve crash reporter, crash reporting product register, and archive accessor
            fs.dir("svc")
                .add_fidl_service(|stream: fcrash::CrashReporterRequestStream| {
                    crash_reporter.clone().serve_async(stream);
                })
                .add_fidl_service(|stream: fcrash::CrashReportingProductRegisterRequestStream| {
                    crash_reporting_product_register.clone().serve_async(stream);
                })
                .add_fidl_service_at(
                    "fuchsia.diagnostics.FeedbackArchiveAccessor",
                    |stream: diagnostics::ArchiveAccessorRequestStream| {
                        archive_accessor.clone().serve_async(stream);
                    },
                );

            fs.serve_connection(mock_handles.outgoing_dir).unwrap();
            fs.collect::<()>().await;
            Ok::<(), anyhow::Error>(())
        }
        .boxed()
    }
}

async fn run_a_test(test_data: TestData) -> Result<(), Error> {
    let start_time = std::time::Instant::now();
    let mut done_waiter = DoneWaiter::new();
    let (events_sender, events_receiver) = mpsc::unbounded();

    let crash_reporter = FakeCrashReporter::new(events_sender.clone(), done_waiter.get_signaler());

    let crash_reporting_product_register =
        FakeCrashReportingProductRegister::new(done_waiter.get_signaler());

    let event_signaler =
        Box::new(ArchiveEventSignaler::new(events_sender.clone(), done_waiter.get_signaler()));
    let archive_accessor = FakeArchiveAccessor::new(&test_data.inspect_data, Some(event_signaler));

    let builder = RealmBuilder::new().await.unwrap();
    let detect =
        builder.add_child("detect", DETECT_PROGRAM_URL, ChildOptions::new().eager()).await.unwrap();

    let mock_component = create_mock_component(
        test_data.clone(),
        crash_reporter.clone(),
        crash_reporting_product_register.clone(),
        archive_accessor.clone(),
    );

    let mocks =
        builder.add_local_child("mocks", mock_component, ChildOptions::new()).await.unwrap();

    // Forward logging to debug test breakages.
    builder
        .add_route(
            Route::new()
                .capability(Capability::protocol_by_name("fuchsia.logger.LogSink"))
                .from(Ref::parent())
                .to(&detect),
        )
        .await
        .unwrap();

    // Forward mocks to detect
    let rights = fio::R_STAR_DIR;
    builder
        .add_route(
            Route::new()
                .capability(Capability::protocol_by_name("fuchsia.feedback.CrashReporter"))
                .capability(Capability::protocol_by_name(
                    "fuchsia.feedback.CrashReportingProductRegister",
                ))
                .capability(Capability::protocol_by_name(
                    "fuchsia.diagnostics.FeedbackArchiveAccessor",
                ))
                .capability(
                    Capability::directory("config-data").path("/config/data").rights(rights),
                )
                .from(&mocks)
                .to(&detect),
        )
        .await
        .unwrap();

    // Register for stopped events
    let mut exit_stream = EventStream::open().await.unwrap();

    // Start the component tree
    let realm_instance = builder.build().await.unwrap();

    // Await the test result.
    if test_data.bails {
        let root_name = realm_instance.root.child_name();
        let moniker = format!(".*{}.*detect$", root_name);
        let exit_future = EventMatcher::ok()
            .stop(None)
            .moniker_regex(moniker)
            .wait::<Stopped>(&mut exit_stream)
            .boxed();

        // If it should bail, exit_stream will tell us it has exited. However,
        // still collect events in case we observe more activity than we should.
        futures::future::select(done_waiter.wait().boxed(), exit_future).await;
    } else {
        // If it shouldn't bail, avoid race conditions by not checking exit_stream.
        // If the program doesn't do what it should and done_waiter never fires,
        // the test will eventually time out and fail.
        done_waiter.wait().await;
    }
    let events = drain(events_receiver).await?;
    let end_time = std::time::Instant::now();
    let minimum_test_time_seconds = CHECK_PERIOD_SECONDS * (test_data.inspect_data.len() as u64);

    // Product-name registration is oneway FIDL - the test can proceed without it
    // having arrived and been recorded. To avoid any possibility of flakes, if the
    // test takes less than 10 seconds, only insist that no error was detected; don't insist that
    // a correct registration has arrived.
    if crash_reporting_product_register.detected_error()
        || (minimum_test_time_seconds >= 10
            && !crash_reporting_product_register.detected_good_registration())
    {
        error!("Test {} failed: Incorrect registration.", test_data.name);
        bail!("Test {} failed: Incorrect registration.", test_data.name);
    }
    let too_fast = end_time - start_time < std::time::Duration::new(minimum_test_time_seconds, 0);
    match evaluate_test_events(&test_data, &events) {
        Ok(()) => {}
        Err(e) => {
            error!("Test {} failed: {}", test_data.name, e);
            bail!("Test {} failed: {}", test_data.name, e);
        }
    }
    if too_fast && !test_data.bails {
        error!("Test {} finished too quickly.", test_data.name);
        bail!("Test {} finished too quickly.", test_data.name);
    }
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

fn evaluate_test_events(test: &TestData, events: &Vec<TestEvent>) -> Result<(), Error> {
    if test.bails {
        match events.len() {
            0 => return Ok(()),
            _ => bail!("{}: Test should bail but events were {:?}", test.name, events),
        }
    }
    let mut crash_list = Vec::new();
    let mut crashes_list = Vec::new();
    let mut events = events.iter();
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
    let mut desired_events = test.snapshots.iter();
    let mut actual_events = crashes_list.iter();
    let mut which_iteration = 0;
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
