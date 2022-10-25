// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::Error;
use argh::FromArgs;
use component_events::{events::*, matcher::*};
use diagnostics_log_encoding::{parse::parse_record, Argument, Record, Severity, Value};
use fidl_fuchsia_diagnostics::Interest;
use fidl_fuchsia_diagnostics_stream::{MAX_ARGS, MAX_ARG_NAME_LENGTH};
use fidl_fuchsia_logger::LogSinkWaitForInterestChangeResponder;
use fidl_fuchsia_logger::{
    LogSinkMarker, LogSinkRequest, LogSinkRequestStream, MAX_DATAGRAM_LEN_BYTES,
};
use fidl_fuchsia_validate_logs::{
    LogSinkPuppetMarker, LogSinkPuppetProxy, PrintfRecordSpec, PrintfValue, PuppetInfo, RecordSpec,
};
use fuchsia_async::{Socket, Task};
use fuchsia_component::server::ServiceFs;
use fuchsia_component_test::{
    Capability, ChildOptions, LocalComponentHandles, RealmBuilder, RealmInstance, Ref, Route,
};
use fuchsia_zircon as zx;
use futures::{channel::mpsc, prelude::*};
use proptest::{
    collection::vec,
    prelude::{any, Arbitrary, Just, ProptestConfig, Strategy, TestCaseError},
    prop_oneof,
    test_runner::{Reason, RngAlgorithm, TestRng, TestRunner},
};
use proptest_derive::Arbitrary;
use std::{collections::BTreeMap, io::Cursor, ops::Range};
use tracing::*;

/// Validate Log VMO formats written by 'puppet' programs controlled by
/// this Validator program.
#[derive(Debug, FromArgs)]
struct Opt {
    /// set to true if you want to use the new file/line rules (where file and line numbers are always included)
    #[argh(switch)]
    new_file_line_rules: bool,
    /// true if you want to test structured printf
    #[argh(switch)]
    test_printf: bool,
    /// true if the runtime supports stopping the interest listener
    #[argh(switch)]
    test_stop_listener: bool,
    /// messages with this tag will be ignored. can be repeated.
    #[argh(option, long = "ignore-tag")]
    ignored_tags: Vec<String>,
    /// if true, invalid unicode will be generated in the initial puppet started message.
    #[argh(switch, long = "test-invalid-unicode")]
    test_invalid_unicode: bool,
}

#[fuchsia::main]
async fn main() -> Result<(), Error> {
    let Opt {
        new_file_line_rules,
        test_printf,
        test_stop_listener,
        ignored_tags,
        test_invalid_unicode,
    } = argh::from_env();
    Puppet::launch(
        new_file_line_rules,
        test_printf,
        test_stop_listener,
        test_invalid_unicode,
        ignored_tags,
    )
    .await?
    .test()
    .await
}

struct Puppet {
    start_time: zx::Time,
    socket: Socket,
    info: PuppetInfo,
    proxy: LogSinkPuppetProxy,
    _puppet_stopped_watchdog: Task<()>,
    new_file_line_rules: bool,
    ignored_tags: Vec<Value>,
    _instance: RealmInstance,
}

async fn demux_fidl(
    stream: &mut LogSinkRequestStream,
) -> Result<(Socket, LogSinkWaitForInterestChangeResponder), Error> {
    let mut interest_listener = None;
    let mut log_socket = None;
    loop {
        match stream.next().await.unwrap()? {
            LogSinkRequest::Connect { socket: _, control_handle: _ } => {
                return Err(anyhow::format_err!("shouldn't ever receive legacy connections"))
            }
            LogSinkRequest::WaitForInterestChange { responder } => {
                interest_listener = Some(responder);
            }
            LogSinkRequest::ConnectStructured { socket, control_handle: _ } => {
                log_socket = Some(socket);
            }
        }
        match (log_socket, interest_listener) {
            (Some(socket), Some(listener)) => {
                return Ok((Socket::from_socket(socket)?, listener));
            }
            (s, i) => {
                log_socket = s;
                interest_listener = i;
            }
        }
    }
}

async fn wait_for_severity(
    stream: &mut LogSinkRequestStream,
) -> Result<LogSinkWaitForInterestChangeResponder, Error> {
    match stream.next().await.unwrap()? {
        LogSinkRequest::WaitForInterestChange { responder } => {
            return Ok(responder);
        }
        _ => return Err(anyhow::format_err!("Unexpected FIDL message")),
    }
}

impl Puppet {
    async fn launch(
        new_file_line_rules: bool,
        has_structured_printf: bool,
        supports_stopping_listener: bool,
        test_invalid_unicode: bool,
        ignored_tags: Vec<String>,
    ) -> Result<Self, Error> {
        let builder = RealmBuilder::new().await?;
        let puppet = builder.add_child("puppet", "#meta/puppet.cm", ChildOptions::new()).await?;
        let (incoming_log_sink_requests_snd, mut incoming_log_sink_requests) = mpsc::unbounded();
        let mocks_server = builder
            .add_local_child(
                "mocks-server",
                move |handles: LocalComponentHandles| {
                    let snd = incoming_log_sink_requests_snd.clone();
                    Box::pin(async move {
                        let mut fs = ServiceFs::new();
                        fs.dir("svc").add_fidl_service(|s: LogSinkRequestStream| {
                            snd.unbounded_send(s).expect("sent log sink request stream");
                        });
                        fs.serve_connection(handles.outgoing_dir)?;
                        fs.collect::<()>().await;
                        Ok(())
                    })
                },
                ChildOptions::new(),
            )
            .await?;
        builder
            .add_route(
                Route::new()
                    .capability(Capability::protocol::<LogSinkPuppetMarker>())
                    .from(&puppet)
                    .to(Ref::parent()),
            )
            .await?;
        builder
            .add_route(
                Route::new()
                    .capability(Capability::protocol::<LogSinkMarker>())
                    .from(&mocks_server)
                    .to(&puppet),
            )
            .await?;

        let mut event_stream = EventStream::open().await.unwrap(); // stopped

        let instance = builder.build().await.expect("create instance");
        let instance_child_name = instance.root.child_name().to_string();
        let _puppet_stopped_watchdog = Task::spawn(async move {
            EventMatcher::ok()
                .moniker(format!("./realm_builder:{}/puppet", instance_child_name))
                .wait::<Stopped>(&mut event_stream)
                .await
                .unwrap();
            let status = event_stream.next().await;
            panic!("puppet should not exit! status: {:?}", status);
        });

        let start_time = zx::Time::get_monotonic();
        let proxy =
            instance.root.connect_to_protocol_at_exposed_dir::<LogSinkPuppetMarker>().unwrap();

        info!("Waiting for first LogSink connection (from Component Manager) (to be ignored).");
        let _ = incoming_log_sink_requests.next().await.unwrap();
        info!("Waiting for second LogSink connection.");
        let mut stream = incoming_log_sink_requests.next().await.unwrap();
        info!("Requesting info from the puppet.");
        let info = proxy.get_info().await?;
        info!("Waiting for LogSink.Connect call.");
        let (socket, interest_listener) = demux_fidl(&mut stream).await?;
        info!("Ensuring we received the init message.");
        assert!(!socket.is_closed());
        let mut puppet = Self {
            socket,
            proxy,
            info,
            start_time,
            _puppet_stopped_watchdog,
            new_file_line_rules,
            ignored_tags: ignored_tags.into_iter().map(Value::Text).collect(),
            _instance: instance,
        };
        if test_invalid_unicode {
            info!("Testing invalid unicode.");
            assert_eq!(
                    puppet.read_record(new_file_line_rules).await?.unwrap(),
                    RecordAssertion::new(&puppet.info, Severity::Info, new_file_line_rules)
                        .add_string("message", "INVALID UTF-8 SEE https://fxbug.dev/88259, message may be corrupted: Puppet started.�(")
                        .build(puppet.start_time..zx::Time::get_monotonic())
                );
        } else {
            info!("Reading regular record.");
            assert_eq!(
                puppet.read_record(new_file_line_rules).await?.unwrap(),
                RecordAssertion::new(&puppet.info, Severity::Info, new_file_line_rules)
                    .add_string("message", "Puppet started.")
                    .build(puppet.start_time..zx::Time::get_monotonic())
            );
        }
        if has_structured_printf {
            info!("Asserting printf record.");
            assert_printf_record(&mut puppet, new_file_line_rules).await?;
        }
        info!("Asserting interest listener");
        assert_interest_listener(
            &mut puppet,
            new_file_line_rules,
            &mut stream,
            &mut incoming_log_sink_requests,
            supports_stopping_listener,
            interest_listener,
        )
        .await?;
        return Ok(puppet);
    }

    async fn read_record(&self, new_file_line_rules: bool) -> Result<Option<TestRecord>, Error> {
        loop {
            let mut buf: Vec<u8> = vec![];
            let bytes_read = self.socket.read_datagram(&mut buf).await.unwrap();
            if bytes_read == 0 {
                continue;
            }
            return TestRecord::parse(&buf[0..bytes_read], new_file_line_rules, &self.ignored_tags);
        }
    }

    // For the CPP puppet it's necessary to strip out the TID from the comparison
    // as interest events happen outside the main thread due to HLCPP.
    async fn read_record_no_tid(&self, expected_tid: u64) -> Result<Option<TestRecord>, Error> {
        loop {
            let mut buf: Vec<u8> = vec![];
            let bytes_read = self.socket.read_datagram(&mut buf).await.unwrap();
            if bytes_read == 0 {
                continue;
            }
            let mut record = TestRecord::parse(
                &buf[0..bytes_read],
                self.new_file_line_rules,
                &self.ignored_tags,
            )?;
            if let Some(record) = record.as_mut() {
                record.arguments.remove("tid");
                record.arguments.insert("tid".to_string(), Value::UnsignedInt(expected_tid));
            }
            return Ok(record);
        }
    }

    async fn test(&self) -> Result<(), Error> {
        info!("Starting the LogSink socket test.");

        let mut runner = TestRunner::new_with_rng(
            ProptestConfig { cases: 2048, failure_persistence: None, ..Default::default() },
            // TODO(fxbug.dev/66783) use TestRng::from_seed
            TestRng::deterministic_rng(RngAlgorithm::ChaCha),
        );

        let (tx_specs, rx_specs) = std::sync::mpsc::channel();
        let (tx_records, rx_records) = std::sync::mpsc::channel();

        // we spawn the testrunner onto a separate thread so that it can synchronously loop through
        // all the test cases and we can still use async to communicate with the puppet.
        // TODO(https://github.com/AltSysrq/proptest/pull/185) proptest async support
        let puppet_info = self.info.clone();
        let new_file_line_rules = self.new_file_line_rules; // needed for lambda capture
        let proptest_thread = std::thread::spawn(move || {
            runner
                .run(&TestVector::arbitrary(), |vector| {
                    let TestCycle { spec, mut assertion } =
                        TestCycle::new(&puppet_info, vector, new_file_line_rules);

                    tx_specs.send(spec).unwrap();
                    let (observed, valid_times) = rx_records.recv().unwrap();
                    let expected = assertion.build(valid_times);

                    if observed == expected {
                        Ok(())
                    } else {
                        Err(TestCaseError::Fail(
                            format!(
                                "unexpected test record: received {:?}, expected {:?}",
                                observed, expected
                            )
                            .into(),
                        ))
                    }
                })
                .unwrap();
        });

        for spec in rx_specs {
            let result = self.run_spec(spec, self.new_file_line_rules).await?;
            tx_records.send(result)?;
        }

        proptest_thread.join().unwrap();
        info!("Tested LogSink socket successfully.");
        Ok(())
    }

    async fn run_spec(
        &self,
        mut spec: RecordSpec,
        new_file_line_rules: bool,
    ) -> Result<(TestRecord, Range<zx::Time>), Error> {
        let before = zx::Time::get_monotonic();
        self.proxy.emit_log(&mut spec).await?;
        let after = zx::Time::get_monotonic();

        // read until we get to a non-ignored record
        let record = loop {
            if let Some(r) = self.read_record(new_file_line_rules).await? {
                break r;
            };
        };

        Ok((record, before..after))
    }
}

fn severity_to_string(severity: Severity) -> String {
    match severity {
        Severity::Trace => "Trace".to_string(),
        Severity::Debug => "Debug".to_string(),
        Severity::Info => "Info".to_string(),
        Severity::Warn => "Warn".to_string(),
        Severity::Error => "Error".to_string(),
        Severity::Fatal => "Fatal".to_string(),
    }
}

async fn assert_logged_severities(
    puppet: &Puppet,
    severities: &[Severity],
    new_file_line_rules: bool,
) -> Result<(), Error> {
    for severity in severities {
        assert_eq!(
            puppet.read_record_no_tid(puppet.info.tid).await?.unwrap(),
            RecordAssertion::new(&puppet.info, *severity, new_file_line_rules)
                .add_string("message", &severity_to_string(*severity))
                .build(puppet.start_time..zx::Time::get_monotonic())
        );
    }
    Ok(())
}

async fn assert_interest_listener<S>(
    puppet: &mut Puppet,
    new_file_line_rules: bool,
    stream: &mut LogSinkRequestStream,
    incoming_log_sink_requests: &mut S,
    supports_stopping_listener: bool,
    listener: LogSinkWaitForInterestChangeResponder,
) -> Result<(), Error>
where
    S: Stream<Item = LogSinkRequestStream> + std::marker::Unpin,
{
    macro_rules! send_log_with_severity {
        ($severity:ident) => {
            let mut record = RecordSpec {
                file: "test_file.cc".to_string(),
                line: 9001,
                record: Record {
                    arguments: vec![Argument {
                        name: "message".to_string(),
                        value: diagnostics_log_encoding::Value::Text(
                            stringify!($severity).to_string(),
                        ),
                    }],
                    severity: Severity::$severity,
                    timestamp: 0,
                },
            };
            puppet.proxy.emit_log(&mut record).await?;
        };
    }
    let interest = Interest { min_severity: Some(Severity::Warn), ..Interest::EMPTY };
    listener.send(&mut Ok(interest))?;
    info!("Waiting for interest....");
    assert_eq!(
        puppet.read_record_no_tid(puppet.info.tid).await?.unwrap(),
        RecordAssertion::new(&puppet.info, Severity::Warn, new_file_line_rules)
            .add_string("message", "Changed severity")
            .build(puppet.start_time..zx::Time::get_monotonic())
    );

    send_log_with_severity!(Debug);
    send_log_with_severity!(Info);
    send_log_with_severity!(Warn);
    send_log_with_severity!(Error);
    assert_logged_severities(&puppet, &[Severity::Warn, Severity::Error], new_file_line_rules)
        .await
        .unwrap();
    info!("Got interest");
    let interest = Interest { min_severity: Some(Severity::Trace), ..Interest::EMPTY };
    let listener = wait_for_severity(stream).await?;
    listener.send(&mut Ok(interest))?;
    assert_eq!(
        puppet.read_record_no_tid(puppet.info.tid).await?.unwrap(),
        RecordAssertion::new(&puppet.info, Severity::Trace, new_file_line_rules)
            .add_string("message", "Changed severity")
            .build(puppet.start_time..zx::Time::get_monotonic())
    );
    send_log_with_severity!(Trace);
    send_log_with_severity!(Debug);
    send_log_with_severity!(Info);
    send_log_with_severity!(Warn);
    send_log_with_severity!(Error);

    assert_logged_severities(
        &puppet,
        &[Severity::Trace, Severity::Debug, Severity::Info, Severity::Warn, Severity::Error],
        new_file_line_rules,
    )
    .await
    .unwrap();

    let interest = Interest::EMPTY;
    let listener = wait_for_severity(stream).await?;
    listener.send(&mut Ok(interest))?;
    info!("Waiting for reset interest....");
    assert_eq!(
        puppet.read_record_no_tid(puppet.info.tid).await?.unwrap(),
        RecordAssertion::new(&puppet.info, Severity::Info, new_file_line_rules)
            .add_string("message", "Changed severity")
            .build(puppet.start_time..zx::Time::get_monotonic())
    );

    send_log_with_severity!(Debug);
    send_log_with_severity!(Info);
    send_log_with_severity!(Warn);
    send_log_with_severity!(Error);

    assert_logged_severities(
        &puppet,
        &[Severity::Info, Severity::Warn, Severity::Error],
        new_file_line_rules,
    )
    .await
    .unwrap();

    if supports_stopping_listener {
        puppet.proxy.stop_interest_listener().await.unwrap();
        // We're restarting the logging system in the child process so we should expect a re-connection.
        *stream = incoming_log_sink_requests.next().await.unwrap();
        if let LogSinkRequest::ConnectStructured { socket, control_handle: _ } =
            stream.next().await.unwrap()?
        {
            puppet.socket = Socket::from_socket(socket)?;
        }
    } else {
        // Reset severity to TRACE so we get all messages
        // in later tests.
        let interest = Interest { min_severity: Some(Severity::Trace), ..Interest::EMPTY };
        let listener = wait_for_severity(stream).await?;
        listener.send(&mut Ok(interest))?;
        info!("Waiting for interest to change back....");
        assert_eq!(
            puppet.read_record_no_tid(puppet.info.tid).await?.unwrap(),
            RecordAssertion::new(&puppet.info, Severity::Trace, new_file_line_rules)
                .add_string("message", "Changed severity")
                .build(puppet.start_time..zx::Time::get_monotonic())
        );
        info!("Changed interest back");
    }
    Ok(())
}

async fn assert_printf_record(puppet: &mut Puppet, new_file_line_rules: bool) -> Result<(), Error> {
    let args = vec![
        PrintfValue::IntegerValue(5),
        PrintfValue::StringValue("test".to_string()),
        PrintfValue::FloatValue(0.5),
    ];
    let record = RecordSpec {
        file: "test_file.cc".to_string(),
        line: 9001,
        record: Record {
            arguments: vec![Argument {
                name: "key".to_string(),
                value: diagnostics_log_encoding::Value::Text("value".to_string()),
            }],
            severity: Severity::Info,
            timestamp: 0,
        },
    };
    let mut spec: PrintfRecordSpec = PrintfRecordSpec {
        record: record,
        printf_arguments: args,
        msg: "Test printf int %i string %s double %f".to_string(),
    };
    puppet.proxy.emit_printf_log(&mut spec).await?;
    info!("Waiting for printf");
    assert_eq!(
        puppet.read_record(new_file_line_rules).await?.unwrap(),
        RecordAssertion::new(&puppet.info, Severity::Info, new_file_line_rules)
            .add_string("message", "Test printf int %i string %s double %f")
            .add_printf(Value::SignedInt(5))
            .add_printf(Value::Text("test".to_string()))
            .add_printf(Value::Floating(0.5))
            .add_string("key", "value")
            .build(puppet.start_time..zx::Time::get_monotonic())
    );
    info!("Got printf");
    Ok(())
}

#[derive(Debug, Arbitrary)]
#[proptest(filter = "vector_filter")]
struct TestVector {
    #[proptest(strategy = "severity_strategy()")]
    severity: Severity,
    #[proptest(strategy = "args_strategy()")]
    args: Vec<(String, Value)>,
}

impl TestVector {
    fn record(&self) -> Record {
        let mut record = Record { arguments: vec![], severity: self.severity, timestamp: 0 };
        for (name, value) in &self.args {
            record.arguments.push(Argument { name: name.clone(), value: value.clone() });
        }
        record
    }
}

fn vector_filter(vector: &TestVector) -> bool {
    // check to make sure the generated message is small enough
    let record = vector.record();
    // TODO(fxbug.dev/66785) avoid this overallocation by supporting growth of the vec
    let mut buf = Cursor::new(vec![0; 1_000_000]);
    {
        diagnostics_log_encoding::encode::Encoder::new(&mut buf).write_record(&record).unwrap();
    }

    buf.position() < MAX_DATAGRAM_LEN_BYTES as u64
}

fn severity_strategy() -> impl Strategy<Value = Severity> {
    prop_oneof![
        Just(Severity::Trace),
        Just(Severity::Debug),
        Just(Severity::Info),
        Just(Severity::Warn),
        Just(Severity::Error),
    ]
}

fn args_strategy() -> impl Strategy<Value = Vec<(String, Value)>> {
    let key_strategy = any::<String>().prop_filter(Reason::from("key too large"), move |s| {
        s.len() <= MAX_ARG_NAME_LENGTH as usize
    });

    let value_strategy = prop_oneof![
        any::<String>().prop_map(|s| Value::Text(s)),
        any::<i64>().prop_map(|n| Value::SignedInt(n)),
        any::<u64>().prop_map(|n| Value::UnsignedInt(n)),
        any::<f64>().prop_map(|f| Value::Floating(f)),
        any::<bool>().prop_map(|f| Value::Boolean(f)),
    ];

    vec((key_strategy, value_strategy), 0..=MAX_ARGS as usize)
}

struct TestCycle {
    spec: RecordSpec,
    assertion: RecordAssertionBuilder,
}

impl TestCycle {
    fn new(info: &PuppetInfo, vector: TestVector, new_file_line_rules: bool) -> Self {
        let spec = RecordSpec { file: "test".to_string(), line: 25, record: vector.record() };
        let mut assertion = RecordAssertion::new(&info, vector.severity, new_file_line_rules);
        for (name, value) in vector.args {
            match value {
                Value::Text(t) => assertion.add_string(&name, &t),
                Value::SignedInt(n) => assertion.add_signed(&name, n),
                Value::UnsignedInt(n) => assertion.add_unsigned(&name, n),
                Value::Floating(f) => assertion.add_floating(&name, f),
                Value::Boolean(f) => assertion.add_boolean(&name, f),
                _ => unreachable!("we don't generate these"),
            };
        }

        Self { spec, assertion }
    }
}

const STUB_ERROR_FILENAME: &str = "path/to/puppet";
const STUB_ERROR_LINE: u64 = 0x1A4;

#[derive(Debug, PartialEq)]
struct TestRecord {
    timestamp: i64,
    severity: Severity,
    arguments: BTreeMap<String, Value>,
    printf_arguments: Vec<Value>,
}

/// State machine used for parsing a record.
enum StateMachine {
    /// Initial state (Printf, ModifiedNormal)
    Init,
    /// Regular parsing case (no special mode such as printf)
    RegularArgs,
    /// Modified parsing case (may switch to printf args)
    NestedRegularArgs,
    /// Inside printf args (may switch to RegularArgs)
    PrintfArgs,
}

impl TestRecord {
    fn parse(
        buf: &[u8],
        new_file_line_rules: bool,
        ignored_tags: &[Value],
    ) -> Result<Option<Self>, Error> {
        let Record { timestamp, severity, arguments } = parse_record(buf)?.0;

        let mut sorted_args = BTreeMap::new();
        let mut printf_arguments = vec![];

        let mut state = StateMachine::Init;
        for Argument { name, value } in arguments {
            // check for ignored tags
            if name == "tag" && ignored_tags.iter().any(|t| t == &value) {
                return Ok(None);
            }

            macro_rules! insert_normal {
                () => {
                    if severity >= Severity::Error || new_file_line_rules {
                        if name == "file" {
                            sorted_args.insert(name, Value::Text(STUB_ERROR_FILENAME.to_owned()));
                            continue;
                        } else if name == "line" {
                            sorted_args.insert(name, Value::UnsignedInt(STUB_ERROR_LINE));
                            continue;
                        }
                    }
                    sorted_args.insert(name, value);
                };
            }
            match state {
                StateMachine::Init if name == "printf" => {
                    state = StateMachine::NestedRegularArgs;
                }
                StateMachine::Init => {
                    insert_normal!();
                    state = StateMachine::RegularArgs;
                }
                StateMachine::RegularArgs => {
                    insert_normal!();
                }
                StateMachine::NestedRegularArgs if name == "" => {
                    state = StateMachine::PrintfArgs;
                    printf_arguments.push(value);
                }
                StateMachine::NestedRegularArgs => {
                    insert_normal!();
                }
                StateMachine::PrintfArgs if name == "" => {
                    printf_arguments.push(value);
                }
                StateMachine::PrintfArgs => {
                    insert_normal!();
                    state = StateMachine::RegularArgs;
                }
            }
        }

        Ok(Some(Self { timestamp, severity, arguments: sorted_args, printf_arguments }))
    }
}
impl PartialEq<RecordAssertion> for TestRecord {
    fn eq(&self, rhs: &RecordAssertion) -> bool {
        rhs.eq(self)
    }
}

#[derive(Debug)]
struct RecordAssertion {
    valid_times: Range<zx::Time>,
    severity: Severity,
    arguments: BTreeMap<String, Value>,
    printf_arguments: Vec<Value>,
}

impl RecordAssertion {
    fn new(
        info: &PuppetInfo,
        severity: Severity,
        new_file_line_rules: bool,
    ) -> RecordAssertionBuilder {
        let mut builder = RecordAssertionBuilder {
            severity,
            arguments: BTreeMap::new(),
            printf_arguments: vec![],
        };
        if let Some(tag) = &info.tag {
            builder.add_string("tag", tag);
        }
        builder.add_unsigned("pid", info.pid);
        builder.add_unsigned("tid", info.tid);
        if severity >= Severity::Error || new_file_line_rules {
            builder.add_string("file", STUB_ERROR_FILENAME);
            builder.add_unsigned("line", STUB_ERROR_LINE);
        }
        builder
    }
}

impl PartialEq<TestRecord> for RecordAssertion {
    fn eq(&self, rhs: &TestRecord) -> bool {
        self.valid_times.contains(&zx::Time::from_nanos(rhs.timestamp))
            && self.severity == rhs.severity
            && self.arguments == rhs.arguments
            && self.printf_arguments == rhs.printf_arguments
    }
}

struct RecordAssertionBuilder {
    severity: Severity,
    arguments: BTreeMap<String, Value>,
    printf_arguments: Vec<Value>,
}

impl RecordAssertionBuilder {
    fn build(&mut self, valid_times: Range<zx::Time>) -> RecordAssertion {
        RecordAssertion {
            valid_times,
            severity: self.severity,
            arguments: std::mem::replace(&mut self.arguments, Default::default()),
            printf_arguments: self.printf_arguments.clone(),
        }
    }

    fn add_printf(&mut self, value: Value) -> &mut Self {
        self.printf_arguments.push(value);
        self
    }

    fn add_string(&mut self, name: &str, value: &str) -> &mut Self {
        self.arguments.insert(name.to_owned(), Value::Text(value.to_owned()));
        self
    }

    fn add_unsigned(&mut self, name: &str, value: u64) -> &mut Self {
        self.arguments.insert(name.to_owned(), Value::UnsignedInt(value));
        self
    }

    fn add_signed(&mut self, name: &str, value: i64) -> &mut Self {
        self.arguments.insert(name.to_owned(), Value::SignedInt(value));
        self
    }

    fn add_floating(&mut self, name: &str, value: f64) -> &mut Self {
        self.arguments.insert(name.to_owned(), Value::Floating(value));
        self
    }

    fn add_boolean(&mut self, name: &str, value: bool) -> &mut Self {
        self.arguments.insert(name.to_owned(), Value::Boolean(value));
        self
    }
}
