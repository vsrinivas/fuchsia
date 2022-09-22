// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! This crate provides helper functions for testing architecture tests.

use {
    anyhow::{bail, Context as _, Error},
    fidl_fuchsia_io as fio,
    fidl_fuchsia_test_manager::{
        self as ftest_manager, SuiteControllerProxy, SuiteEvent as FidlSuiteEvent,
        SuiteEventPayload as FidlSuiteEventPayload, SuiteEventPayloadUnknown,
    },
    fuchsia_async as fasync,
    futures::{channel::mpsc, prelude::*},
    linked_hash_map::LinkedHashMap,
    std::{collections::HashMap, sync::Arc},
    test_diagnostics::{collect_and_send_string_output, LogStream},
    tracing::*,
};

/// Builds and runs test suite(s).
pub struct TestBuilder {
    proxy: ftest_manager::RunBuilderProxy,
    filter_debug_data: bool,
}

impl TestBuilder {
    /// Create new instance
    pub fn new(proxy: ftest_manager::RunBuilderProxy) -> Self {
        Self { proxy, filter_debug_data: false }
    }

    /// Filter out debug data. On coverage builders, tests executed under
    /// test_manager produce coverage profile. This option is useful for
    /// ignoring these and ensuring the caller observes the same events on
    /// all builders.
    pub fn filter_debug_data(self) -> Self {
        let Self { proxy, .. } = self;
        Self { proxy, filter_debug_data: true }
    }

    pub fn take_proxy(self) -> ftest_manager::RunBuilderProxy {
        self.proxy
    }

    pub fn set_scheduling_options(&self, accumulate_debug_data: bool) -> Result<(), Error> {
        self.proxy
            .with_scheduling_options(ftest_manager::SchedulingOptions {
                accumulate_debug_data: Some(accumulate_debug_data),
                ..ftest_manager::SchedulingOptions::EMPTY
            })
            .map_err(Error::from)
    }

    /// Add suite to run.
    pub async fn add_suite(
        &self,
        test_url: &str,
        run_options: ftest_manager::RunOptions,
    ) -> Result<SuiteRunInstance, Error> {
        let (controller_proxy, controller) =
            fidl::endpoints::create_proxy().context("Cannot create proxy")?;
        self.proxy.add_suite(test_url, run_options, controller)?;
        Ok(SuiteRunInstance { controller_proxy: controller_proxy.into() })
    }

    /// Runs all tests to completion and collects events.
    pub async fn run(self) -> Result<Vec<TestRunEvent>, Error> {
        let (controller_proxy, controller) =
            fidl::endpoints::create_proxy().context("Cannot create proxy")?;
        self.proxy.build(controller).context("Error starting tests")?;
        // wait for test to end
        let mut events = vec![];
        loop {
            let fidl_events = controller_proxy.get_events().await.context("Get run events")?;
            if fidl_events.is_empty() {
                break;
            }
            for fidl_event in fidl_events {
                match fidl_event.payload.expect("Payload cannot be empty") {
                    ftest_manager::RunEventPayload::Artifact(
                        ftest_manager::Artifact::DebugData(iterator),
                    ) => {
                        if !self.filter_debug_data {
                            let proxy = iterator.into_proxy().context("Create proxy")?;
                            loop {
                                let data = proxy.get_next().await?;
                                if data.is_empty() {
                                    break;
                                }
                                for data_file in data {
                                    let file_proxy = data_file
                                        .file
                                        .expect("File cannot be empty")
                                        .into_proxy()?;
                                    events.push(TestRunEvent::debug_data(
                                        fidl_event.timestamp,
                                        data_file.name.expect("Name cannot be empty"),
                                        file_proxy,
                                    ));
                                }
                            }
                        }
                    }
                    other => bail!("Expected only debug data run events but got {:?}", other),
                }
            }
        }
        Ok(events)
    }
}

#[derive(Debug)]
pub struct TestRunEvent {
    pub timestamp: Option<i64>,
    pub payload: TestRunEventPayload,
}

impl TestRunEvent {
    pub fn debug_data<S: Into<String>>(
        timestamp: Option<i64>,
        filename: S,
        proxy: fio::FileProxy,
    ) -> Self {
        Self {
            timestamp,
            payload: TestRunEventPayload::DebugData { filename: filename.into(), proxy },
        }
    }
}

#[derive(Debug)]
pub enum TestRunEventPayload {
    DebugData { filename: String, proxy: fio::FileProxy },
}

/// Events produced by test suite.
pub struct SuiteEvent {
    pub timestamp: Option<i64>,
    pub payload: SuiteEventPayload,
}

impl SuiteEvent {
    pub fn case_found(timestamp: Option<i64>, name: String) -> Self {
        SuiteEvent { timestamp, payload: SuiteEventPayload::RunEvent(RunEvent::case_found(name)) }
    }

    pub fn case_started(timestamp: Option<i64>, name: String) -> Self {
        SuiteEvent { timestamp, payload: SuiteEventPayload::RunEvent(RunEvent::case_started(name)) }
    }

    pub fn case_stdout<N, L>(timestamp: Option<i64>, name: N, stdout_message: L) -> Self
    where
        N: Into<String>,
        L: Into<String>,
    {
        SuiteEvent {
            timestamp,
            payload: SuiteEventPayload::RunEvent(RunEvent::case_stdout(
                name.into(),
                stdout_message.into(),
            )),
        }
    }

    pub fn case_stderr<N, L>(timestamp: Option<i64>, name: N, stderr_message: L) -> Self
    where
        N: Into<String>,
        L: Into<String>,
    {
        SuiteEvent {
            timestamp,
            payload: SuiteEventPayload::RunEvent(RunEvent::case_stderr(
                name.into(),
                stderr_message.into(),
            )),
        }
    }

    pub fn case_stopped(
        timestamp: Option<i64>,
        name: String,
        status: ftest_manager::CaseStatus,
    ) -> Self {
        SuiteEvent {
            timestamp,
            payload: SuiteEventPayload::RunEvent(RunEvent::case_stopped(name, status)),
        }
    }

    pub fn case_finished(timestamp: Option<i64>, name: String) -> Self {
        SuiteEvent {
            timestamp,
            payload: SuiteEventPayload::RunEvent(RunEvent::case_finished(name)),
        }
    }

    pub fn suite_stopped(timestamp: Option<i64>, status: ftest_manager::SuiteStatus) -> Self {
        SuiteEvent {
            timestamp,
            payload: SuiteEventPayload::RunEvent(RunEvent::suite_stopped(status)),
        }
    }

    pub fn suite_custom(
        timestamp: Option<i64>,
        component: String,
        filename: String,
        contents: String,
    ) -> Self {
        SuiteEvent {
            timestamp,
            payload: SuiteEventPayload::RunEvent(RunEvent::suite_custom(
                component, filename, contents,
            )),
        }
    }

    pub fn suite_log(timestamp: Option<i64>, log_stream: LogStream) -> Self {
        SuiteEvent { timestamp, payload: SuiteEventPayload::SuiteLog { log_stream } }
    }

    pub fn test_case_log(timestamp: Option<i64>, name: String, log_stream: LogStream) -> Self {
        SuiteEvent { timestamp, payload: SuiteEventPayload::TestCaseLog { name, log_stream } }
    }
}

pub enum SuiteEventPayload {
    /// Logger for test suite
    SuiteLog { log_stream: LogStream },

    /// Logger for a test case in suite.
    TestCaseLog { name: String, log_stream: LogStream },

    /// Test events.
    RunEvent(RunEvent),
}

#[derive(PartialEq, Debug, Eq, Hash, Ord, PartialOrd, Clone)]
pub enum RunEvent {
    CaseFound { name: String },
    CaseStarted { name: String },
    CaseStdout { name: String, stdout_message: String },
    CaseStderr { name: String, stderr_message: String },
    CaseStopped { name: String, status: ftest_manager::CaseStatus },
    CaseFinished { name: String },
    SuiteStarted,
    SuiteCustom { component: String, filename: String, contents: String },
    SuiteStopped { status: ftest_manager::SuiteStatus },
}

impl RunEvent {
    pub fn case_found<S>(name: S) -> Self
    where
        S: Into<String>,
    {
        Self::CaseFound { name: name.into() }
    }

    pub fn case_started<S>(name: S) -> Self
    where
        S: Into<String>,
    {
        Self::CaseStarted { name: name.into() }
    }

    pub fn case_stdout<S, L>(name: S, stdout_message: L) -> Self
    where
        S: Into<String>,
        L: Into<String>,
    {
        Self::CaseStdout { name: name.into(), stdout_message: stdout_message.into() }
    }

    pub fn case_stderr<S, L>(name: S, stderr_message: L) -> Self
    where
        S: Into<String>,
        L: Into<String>,
    {
        Self::CaseStderr { name: name.into(), stderr_message: stderr_message.into() }
    }

    pub fn case_stopped<S>(name: S, status: ftest_manager::CaseStatus) -> Self
    where
        S: Into<String>,
    {
        Self::CaseStopped { name: name.into(), status }
    }

    pub fn case_finished<S>(name: S) -> Self
    where
        S: Into<String>,
    {
        Self::CaseFinished { name: name.into() }
    }

    pub fn suite_started() -> Self {
        Self::SuiteStarted
    }

    pub fn suite_custom<T, U, V>(component: T, filename: U, contents: V) -> Self
    where
        T: Into<String>,
        U: Into<String>,
        V: Into<String>,
    {
        Self::SuiteCustom {
            component: component.into(),
            filename: filename.into(),
            contents: contents.into(),
        }
    }

    pub fn suite_stopped(status: ftest_manager::SuiteStatus) -> Self {
        Self::SuiteStopped { status }
    }
    /// Returns the name of the test case to which the event belongs, if applicable.
    pub fn test_case_name(&self) -> Option<&String> {
        match self {
            RunEvent::CaseFound { name }
            | RunEvent::CaseStarted { name }
            | RunEvent::CaseStdout { name, .. }
            | RunEvent::CaseStderr { name, .. }
            | RunEvent::CaseStopped { name, .. }
            | RunEvent::CaseFinished { name } => Some(name),
            RunEvent::SuiteStarted
            | RunEvent::SuiteStopped { .. }
            | RunEvent::SuiteCustom { .. } => None,
        }
    }

    /// Same as `test_case_name`, but returns an owned `Option<String>`.
    pub fn owned_test_case_name(&self) -> Option<String> {
        self.test_case_name().map(String::from)
    }
}

/// Groups events by stdout, stderr and non stdout/stderr events to make it easy to compare them
/// in tests.
#[derive(Default, Debug, Eq, PartialEq, Clone)]
pub struct GroupedRunEvents {
    // order of events is maintained.
    pub non_artifact_events: Vec<RunEvent>,
    // order of stdout events is maintained.
    pub stdout_events: Vec<RunEvent>,
    // order of stderr events is maintained.
    pub stderr_events: Vec<RunEvent>,
}

/// Trait allowing iterators over `RunEvent` to be partitioned by test case name.
pub trait GroupRunEventByTestCase: Iterator<Item = RunEvent> + Sized {
    /// Groups the `RunEvent`s by test case name into a map that preserves insertion order of
    /// various types of events.
    /// The overall order of test cases (by first event) and the orders of events within each test
    /// case are preserved, but events from different test cases are effectively de-interleaved.
    ///
    /// Example:
    /// ```rust
    /// use test_diagnostics::{RunEvent, GroupRunEventByTestCase as _};
    /// use linked_hash_map::LinkedHashMap;
    ///
    /// let events: Vec<RunEvent> = get_events();
    /// let grouped: LinkedHashMap<Option<String>, GroupedRunEvents> =
    ///     events.into_iter().group_by_test_case_ordered();
    /// ```
    fn group_by_test_case_ordered(self) -> LinkedHashMap<Option<String>, GroupedRunEvents> {
        let mut map = LinkedHashMap::new();
        for run_event in self {
            match run_event {
                RunEvent::CaseStderr { .. } => map
                    .entry(run_event.owned_test_case_name())
                    .or_insert(GroupedRunEvents::default())
                    .stderr_events
                    .push(run_event),

                RunEvent::CaseStdout { .. } => map
                    .entry(run_event.owned_test_case_name())
                    .or_insert(GroupedRunEvents::default())
                    .stdout_events
                    .push(run_event),

                _ => map
                    .entry(run_event.owned_test_case_name())
                    .or_insert(GroupedRunEvents::default())
                    .non_artifact_events
                    .push(run_event),
            }
        }
        map
    }

    /// Groups the `RunEvent`s by test case name into an unordered map. The orders of events within
    /// each test case are preserved, but the test cases themselves are not in a defined order.
    fn group_by_test_case_unordered(self) -> HashMap<Option<String>, GroupedRunEvents> {
        let mut map = HashMap::new();
        for run_event in self {
            match run_event {
                RunEvent::CaseStderr { .. } => map
                    .entry(run_event.owned_test_case_name())
                    .or_insert(GroupedRunEvents::default())
                    .stderr_events
                    .push(run_event),

                RunEvent::CaseStdout { .. } => map
                    .entry(run_event.owned_test_case_name())
                    .or_insert(GroupedRunEvents::default())
                    .stdout_events
                    .push(run_event),

                _ => map
                    .entry(run_event.owned_test_case_name())
                    .or_insert(GroupedRunEvents::default())
                    .non_artifact_events
                    .push(run_event),
            }
        }
        map
    }

    /// Group `RunEvent`s by stdout, stderr and non-stdout/err events and returns `GroupedRunEvents`.
    fn group(self) -> GroupedRunEvents {
        let mut events = GroupedRunEvents::default();
        for run_event in self {
            match run_event {
                RunEvent::CaseStderr { .. } => events.stderr_events.push(run_event),

                RunEvent::CaseStdout { .. } => events.stdout_events.push(run_event),

                _ => events.non_artifact_events.push(run_event),
            }
        }
        events
    }
}

impl<T> GroupRunEventByTestCase for T where T: Iterator<Item = RunEvent> + Sized {}

#[derive(Default)]
struct FidlSuiteEventProcessor {
    case_map: HashMap<u32, String>,
    std_output_map: HashMap<u32, Vec<fasync::Task<Result<(), Error>>>>,
}

impl FidlSuiteEventProcessor {
    fn new() -> Self {
        FidlSuiteEventProcessor::default()
    }

    fn get_test_case_name(&self, identifier: u32) -> String {
        self.case_map
            .get(&identifier)
            .expect(&format!("invalid test case identifier: {}", identifier))
            .clone()
    }

    async fn process(
        &mut self,
        event: FidlSuiteEvent,
        mut sender: mpsc::Sender<SuiteEvent>,
    ) -> Result<(), Error> {
        let timestamp = event.timestamp;
        let e = match event.payload.expect("Payload cannot be null, please file bug.") {
            FidlSuiteEventPayload::CaseFound(cf) => {
                self.case_map.insert(cf.identifier, cf.test_case_name.clone());
                SuiteEvent::case_found(timestamp, cf.test_case_name).into()
            }
            FidlSuiteEventPayload::CaseStarted(cs) => {
                let test_case_name = self.get_test_case_name(cs.identifier);
                SuiteEvent::case_started(timestamp, test_case_name).into()
            }
            FidlSuiteEventPayload::CaseStopped(cs) => {
                let test_case_name = self.get_test_case_name(cs.identifier);
                if let Some(outputs) = self.std_output_map.remove(&cs.identifier) {
                    for s in outputs {
                        s.await.context(format!(
                            "error collecting stdout/stderr of {}",
                            test_case_name
                        ))?;
                    }
                }
                SuiteEvent::case_stopped(timestamp, test_case_name, cs.status).into()
            }
            FidlSuiteEventPayload::CaseFinished(cf) => {
                let test_case_name = self.get_test_case_name(cf.identifier);
                SuiteEvent::case_finished(timestamp, test_case_name).into()
            }
            FidlSuiteEventPayload::CaseArtifact(ca) => {
                let name = self.get_test_case_name(ca.identifier);
                match ca.artifact {
                    ftest_manager::Artifact::Stdout(stdout) => {
                        let (s, mut r) = mpsc::channel(1024);
                        let stdout_task =
                            fasync::Task::spawn(collect_and_send_string_output(stdout, s));
                        let mut sender_clone = sender.clone();
                        let send_stdout_task = fasync::Task::spawn(async move {
                            while let Some(msg) = r.next().await {
                                sender_clone
                                    .send(SuiteEvent::case_stdout(None, &name, msg))
                                    .await
                                    .context(format!("cannot send logs for {}", name))?;
                            }
                            Ok(())
                        });
                        match self.std_output_map.get_mut(&ca.identifier) {
                            Some(v) => {
                                v.push(stdout_task);
                                v.push(send_stdout_task);
                            }
                            None => {
                                self.std_output_map
                                    .insert(ca.identifier, vec![stdout_task, send_stdout_task]);
                            }
                        }
                        None
                    }
                    ftest_manager::Artifact::Stderr(stderr) => {
                        let (s, mut r) = mpsc::channel(1024);
                        let stderr_task =
                            fasync::Task::spawn(collect_and_send_string_output(stderr, s));
                        let mut sender_clone = sender.clone();
                        let send_stderr_task = fasync::Task::spawn(async move {
                            while let Some(msg) = r.next().await {
                                sender_clone
                                    .send(SuiteEvent::case_stderr(None, &name, msg))
                                    .await
                                    .context(format!("cannot send logs for {}", name))?;
                            }
                            Ok(())
                        });
                        match self.std_output_map.get_mut(&ca.identifier) {
                            Some(v) => {
                                v.push(stderr_task);
                                v.push(send_stderr_task);
                            }
                            None => {
                                self.std_output_map
                                    .insert(ca.identifier, vec![stderr_task, send_stderr_task]);
                            }
                        }
                        None
                    }
                    ftest_manager::Artifact::Log(log) => match LogStream::from_syslog(log) {
                        Ok(log_stream) => {
                            SuiteEvent::test_case_log(timestamp, name, log_stream).into()
                        }
                        Err(e) => {
                            warn!("Cannot collect logs for test suite: {:?}", e);
                            None
                        }
                    },
                    _ => {
                        panic!("not supported")
                    }
                }
            }
            FidlSuiteEventPayload::SuiteArtifact(sa) => match sa.artifact {
                ftest_manager::Artifact::Stdout(_) => {
                    panic!("not supported")
                }
                ftest_manager::Artifact::Stderr(_) => {
                    panic!("not supported")
                }
                ftest_manager::Artifact::Log(log) => match LogStream::from_syslog(log) {
                    Ok(log_stream) => SuiteEvent::suite_log(timestamp, log_stream).into(),
                    Err(e) => {
                        warn!("Cannot collect logs for test suite: {:?}", e);
                        None
                    }
                },
                ftest_manager::Artifact::Custom(custom_artifact) => {
                    let ftest_manager::DirectoryAndToken { directory, token } =
                        custom_artifact.directory_and_token.unwrap();
                    let component_moniker = custom_artifact.component_moniker.unwrap();
                    let mut sender_clone = sender.clone();
                    fasync::Task::spawn(async move {
                        let directory = directory.into_proxy().unwrap();
                        let entries: Vec<_> =
                            fuchsia_fs::directory::readdir_recursive(&directory, None)
                                .try_collect()
                                .await
                                .expect("read custom artifact directory");
                        for entry in entries.into_iter() {
                            let file = fuchsia_fs::open_file(
                                &directory,
                                entry.name.as_ref(),
                                fio::OpenFlags::RIGHT_READABLE,
                            )
                            .unwrap();
                            let contents = fuchsia_fs::read_file(&file).await.unwrap();
                            sender_clone
                                .send(SuiteEvent::suite_custom(
                                    timestamp,
                                    component_moniker.clone(),
                                    entry.name,
                                    contents,
                                ))
                                .await
                                .unwrap();
                        }
                        // Drop the token here - we must keep the token open for the duration that
                        // the directory is in use.
                        drop(token);
                    })
                    .detach();
                    None
                }
                _ => {
                    panic!("not supported")
                }
            },
            FidlSuiteEventPayload::SuiteStarted(_started) => SuiteEvent {
                timestamp,
                payload: SuiteEventPayload::RunEvent(RunEvent::SuiteStarted),
            }
            .into(),
            FidlSuiteEventPayload::SuiteStopped(stopped) => SuiteEvent {
                timestamp,
                payload: SuiteEventPayload::RunEvent(RunEvent::SuiteStopped {
                    status: stopped.status,
                }),
            }
            .into(),
            SuiteEventPayloadUnknown!() => panic!("Unrecognized SuiteEvent"),
        };
        if let Some(item) = e {
            sender.send(item).await.context("Cannot send event")?;
        }
        Ok(())
    }
}

#[derive(Debug, thiserror::Error, Eq, PartialEq, Copy, Clone)]
pub enum SuiteLaunchError {
    #[error("Cannot enumerate tests")]
    CaseEnumeration,

    #[error("Cannot resolve test url")]
    InstanceCannotResolve,

    #[error("Invalid arguments passed")]
    InvalidArgs,

    #[error("Cannot connect to test suite")]
    FailedToConnectToTestSuite,

    #[error("resource unavailable")]
    ResourceUnavailable,

    #[error("Some internal error occurred. Please file bug")]
    InternalError,

    #[error("No test cases matched the provided filters")]
    NoMatchingCases,
}

impl From<ftest_manager::LaunchError> for SuiteLaunchError {
    fn from(error: ftest_manager::LaunchError) -> Self {
        match error {
            ftest_manager::LaunchError::ResourceUnavailable => {
                SuiteLaunchError::ResourceUnavailable
            }
            ftest_manager::LaunchError::InstanceCannotResolve => {
                SuiteLaunchError::InstanceCannotResolve
            }
            ftest_manager::LaunchError::InvalidArgs => SuiteLaunchError::InvalidArgs,
            ftest_manager::LaunchError::FailedToConnectToTestSuite => {
                SuiteLaunchError::FailedToConnectToTestSuite
            }
            ftest_manager::LaunchError::CaseEnumeration => SuiteLaunchError::CaseEnumeration,
            ftest_manager::LaunchError::InternalError => SuiteLaunchError::InternalError,
            ftest_manager::LaunchError::NoMatchingCases => SuiteLaunchError::NoMatchingCases,
            ftest_manager::LaunchErrorUnknown!() => panic!("Encountered unknown launch error"),
        }
    }
}

/// Instance to control a single test suite run.
pub struct SuiteRunInstance {
    controller_proxy: Arc<SuiteControllerProxy>,
}

impl SuiteRunInstance {
    pub fn controller(&self) -> Arc<SuiteControllerProxy> {
        self.controller_proxy.clone()
    }

    pub async fn collect_events(&self, sender: mpsc::Sender<SuiteEvent>) -> Result<(), Error> {
        let controller_proxy = self.controller_proxy.clone();
        let mut processor = FidlSuiteEventProcessor::new();
        loop {
            match controller_proxy.get_events().await? {
                Err(e) => return Err(SuiteLaunchError::from(e).into()),
                Ok(events) => {
                    if events.len() == 0 {
                        break;
                    }
                    for event in events {
                        if let Err(e) = processor.process(event, sender.clone()).await {
                            warn!("error running test suite: {:?}", e);
                            let _ = controller_proxy.kill();
                            return Ok(());
                        }
                    }
                }
            }
        }
        Ok(())
    }
}
