// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::Error;
use argh::FromArgs;
use diagnostics_log_encoding::{parse::parse_record, Argument, Record, Severity, Value};
use fidl::endpoints::RequestStream;
use fidl_fuchsia_diagnostics::Interest;
use fidl_fuchsia_diagnostics_stream::{MAX_ARGS, MAX_ARG_NAME_LENGTH};
use fidl_fuchsia_logger::{LogSinkRequest, LogSinkRequestStream, MAX_DATAGRAM_LEN_BYTES};
use fidl_fuchsia_sys::EnvironmentControllerProxy;
use fidl_fuchsia_validate_logs::{
    LogSinkPuppetMarker, LogSinkPuppetProxy, PrintfRecordSpec, PrintfValue, PuppetInfo, RecordSpec,
};
use fuchsia_async::{Socket, Task};
use fuchsia_component::server::ServiceFs;
use fuchsia_component::server::ServiceObj;
use fuchsia_zircon as zx;
use futures::prelude::*;
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
    /// required arg: The URL of the puppet
    #[argh(option, long = "url")]
    puppet_url: String,
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
}

#[fuchsia::component]
async fn main() -> Result<(), Error> {
    let Opt { puppet_url, new_file_line_rules, test_printf, test_stop_listener, ignored_tags } =
        argh::from_env();
    Puppet::launch(&puppet_url, new_file_line_rules, test_printf, test_stop_listener, ignored_tags)
        .await?
        .test()
        .await
}

struct Puppet {
    start_time: zx::Time,
    socket: Socket,
    info: PuppetInfo,
    proxy: LogSinkPuppetProxy,
    _app_watchdog: Task<()>,
    _env: EnvironmentControllerProxy,
    new_file_line_rules: bool,
    ignored_tags: Vec<Value>,
}

impl Puppet {
    async fn launch(
        puppet_url: &str,
        new_file_line_rules: bool,
        has_structured_printf: bool,
        supports_stopping_listener: bool,
        ignored_tags: Vec<String>,
    ) -> Result<Self, Error> {
        let mut fs = ServiceFs::new();
        fs.add_fidl_service(|s: LogSinkRequestStream| s);

        let start_time = zx::Time::get_monotonic();
        info!(%puppet_url, ?ignored_tags, "launching");
        let (_env, mut app) = fs
            .launch_component_in_nested_environment(
                puppet_url.to_owned(),
                None,
                "log_validator_puppet",
            )
            .unwrap();

        fs.take_and_serve_directory_handle()?;

        info!("Connecting to puppet and spawning watchdog.");
        let proxy = app.connect_to_protocol::<LogSinkPuppetMarker>()?;
        let _app_watchdog = Task::spawn(async move {
            let status = app.wait().await;
            panic!("puppet should not exit! status: {:?}", status);
        });

        info!("Waiting for LogSink connection.");
        let mut stream = fs.next().await.unwrap();
        info!("Requesting info from the puppet.");
        let info = proxy.get_info().await?;
        info!("Waiting for LogSink.Connect call.");
        if let LogSinkRequest::ConnectStructured { socket, control_handle: _ } =
            stream.next().await.unwrap()?
        {
            info!("Received a structured socket, ensuring in datagram mode.");
            assert!(
                socket.info()?.options.contains(zx::SocketOpts::DATAGRAM),
                "log sockets must be created in datagram mode"
            );

            info!("Ensuring we received the init message.");
            let mut puppet = Self {
                socket: Socket::from_socket(socket)?,
                proxy,
                info,
                start_time,
                _app_watchdog,
                _env,
                new_file_line_rules,
                ignored_tags: ignored_tags.into_iter().map(Value::Text).collect(),
            };

            assert_eq!(
                puppet.read_record(new_file_line_rules).await?.unwrap(),
                RecordAssertion::new(&puppet.info, Severity::Info, new_file_line_rules)
                    .add_string("message", "Puppet started.")
                    .build(puppet.start_time..zx::Time::get_monotonic())
            );
            if has_structured_printf {
                assert_printf_record(&mut puppet, new_file_line_rules).await?;
            }
            assert_interest_listener(
                &mut puppet,
                new_file_line_rules,
                &mut stream,
                &mut fs,
                supports_stopping_listener,
            )
            .await?;
            Ok(puppet)
        } else {
            Err(anyhow::format_err!("shouldn't ever receive legacy connections"))
        }
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

async fn assert_interest_listener(
    puppet: &mut Puppet,
    new_file_line_rules: bool,
    stream: &mut LogSinkRequestStream,
    fs: &mut ServiceFs<ServiceObj<'_, LogSinkRequestStream>>,
    supports_stopping_listener: bool,
) -> Result<(), Error> {
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
    let handle = stream.control_handle();
    handle.send_on_register_interest(interest)?;
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
    assert_eq!(
        puppet.read_record_no_tid(puppet.info.tid).await?.unwrap(),
        RecordAssertion::new(&puppet.info, Severity::Warn, new_file_line_rules)
            .add_string("message", "Warn")
            .build(puppet.start_time..zx::Time::get_monotonic())
    );
    assert_eq!(
        puppet.read_record_no_tid(puppet.info.tid).await?.unwrap(),
        RecordAssertion::new(&puppet.info, Severity::Error, new_file_line_rules)
            .add_string("message", "Error")
            .build(puppet.start_time..zx::Time::get_monotonic())
    );
    info!("Got interest");
    if supports_stopping_listener {
        puppet.proxy.stop_interest_listener().await.unwrap();
        // We're restarting the logging system in the child process so we should expect a re-connection.
        *stream = fs.next().await.unwrap();
        if let LogSinkRequest::ConnectStructured { socket, control_handle: _ } =
            stream.next().await.unwrap()?
        {
            puppet.socket = Socket::from_socket(socket)?;
        }
    } else {
        // Reset severity to TRACE so we get all messages
        // in later tests.
        let interest = Interest { min_severity: Some(Severity::Trace), ..Interest::EMPTY };
        handle.send_on_register_interest(interest)?;
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
}
