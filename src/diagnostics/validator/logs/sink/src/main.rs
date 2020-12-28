// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::Error;
use argh::FromArgs;
use diagnostics_stream::{parse::parse_record, Argument, Record, Severity, Value};
use fidl_fuchsia_diagnostics_stream::{MAX_ARGS, MAX_ARG_NAME_LENGTH};
use fidl_fuchsia_logger::{LogSinkRequest, LogSinkRequestStream, MAX_DATAGRAM_LEN_BYTES};
use fidl_fuchsia_sys::EnvironmentControllerProxy;
use fidl_fuchsia_validate_logs::{LogSinkPuppetMarker, LogSinkPuppetProxy, PuppetInfo, RecordSpec};
use fuchsia_async::{Socket, Task};
use fuchsia_component::server::ServiceFs;
use fuchsia_zircon as zx;
use futures::prelude::*;
use proptest::{
    collection::vec,
    prelude::{any, Arbitrary, Just, ProptestConfig, Strategy, TestCaseError},
    prop_oneof,
    test_runner::{Reason, RngAlgorithm, TestRng, TestRunner},
};
use proptest_derive::Arbitrary;
use std::io::Cursor;
use std::{collections::BTreeMap, ops::Range};
use tracing::*;

/// Validate Log VMO formats written by 'puppet' programs controlled by
/// this Validator program.
#[derive(Debug, FromArgs)]
struct Opt {
    /// required arg: The URL of the puppet
    #[argh(option, long = "url")]
    puppet_url: String,
}

#[fuchsia_async::run_singlethreaded]
async fn main() -> Result<(), Error> {
    fuchsia_syslog::init_with_tags(&[]).unwrap();
    let Opt { puppet_url } = argh::from_env();
    Puppet::launch(&puppet_url).await?.test().await
}

struct Puppet {
    start_time: zx::Time,
    socket: Socket,
    info: PuppetInfo,
    proxy: LogSinkPuppetProxy,
    _app_watchdog: Task<()>,
    _env: EnvironmentControllerProxy,
}

impl Puppet {
    async fn launch(puppet_url: &str) -> Result<Self, Error> {
        let mut fs = ServiceFs::new();
        fs.add_fidl_service(|s: LogSinkRequestStream| s);

        let start_time = zx::Time::get_monotonic();
        info!(%puppet_url, "launching");
        let (_env, mut app) = fs
            .launch_component_in_nested_environment(
                puppet_url.to_owned(),
                None,
                "log_validator_puppet",
            )
            .unwrap();

        fs.take_and_serve_directory_handle()?;

        info!("Connecting to puppet and spawning watchdog.");
        let proxy = app.connect_to_service::<LogSinkPuppetMarker>()?;
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
            let puppet = Self {
                socket: Socket::from_socket(socket)?,
                proxy,
                info,
                start_time,
                _app_watchdog,
                _env,
            };

            assert_eq!(
                puppet.read_record().await?,
                RecordAssertion::new(&puppet.info, Severity::Info)
                    .add_string("message", "Puppet started.")
                    .build(puppet.start_time..zx::Time::get_monotonic())
            );

            Ok(puppet)
        } else {
            Err(anyhow::format_err!("shouldn't ever receive legacy connections"))
        }
    }

    async fn read_record(&self) -> Result<TestRecord, Error> {
        let mut buf: Vec<u8> = vec![];
        let bytes_read = self.socket.read_datagram(&mut buf).await.unwrap();
        TestRecord::parse(&buf[0..bytes_read])
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
        let proptest_thread = std::thread::spawn(move || {
            runner
                .run(&TestVector::arbitrary(), |vector| {
                    let TestCycle { spec, mut assertion } = TestCycle::new(&puppet_info, vector);

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
            let result = self.run_spec(spec).await?;
            tx_records.send(result)?;
        }

        proptest_thread.join().unwrap();

        info!("Tested LogSink socket successfully.");
        Ok(())
    }

    async fn run_spec(&self, mut spec: RecordSpec) -> Result<(TestRecord, Range<zx::Time>), Error> {
        let before = zx::Time::get_monotonic();
        self.proxy.emit_log(&mut spec).await?;
        let after = zx::Time::get_monotonic();
        let record = self.read_record().await?;
        Ok((record, before..after))
    }
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
        diagnostics_stream::encode::Encoder::new(&mut buf).write_record(&record).unwrap();
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
    fn new(info: &PuppetInfo, vector: TestVector) -> Self {
        let spec = RecordSpec { file: "test".to_string(), line: 25, record: vector.record() };
        let mut assertion = RecordAssertion::new(&info, vector.severity);
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
}

impl TestRecord {
    fn parse(buf: &[u8]) -> Result<Self, Error> {
        let Record { timestamp, severity, arguments } = parse_record(buf)?.0;

        let mut sorted_args = BTreeMap::new();
        for diagnostics_stream::Argument { name, value } in arguments {
            if severity >= Severity::Error {
                if name == "file" {
                    sorted_args.insert(name, Value::Text(STUB_ERROR_FILENAME.to_owned()));
                    continue;
                } else if name == "line" {
                    sorted_args.insert(name, Value::UnsignedInt(STUB_ERROR_LINE));
                    continue;
                }
            }
            sorted_args.insert(name, value);
        }

        Ok(Self { timestamp, severity, arguments: sorted_args })
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
}

impl RecordAssertion {
    fn new(info: &PuppetInfo, severity: Severity) -> RecordAssertionBuilder {
        let mut builder = RecordAssertionBuilder { severity, arguments: BTreeMap::new() };
        builder.add_unsigned("pid", info.pid);
        builder.add_unsigned("tid", info.tid);

        if severity >= Severity::Error {
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
    }
}

struct RecordAssertionBuilder {
    severity: Severity,
    arguments: BTreeMap<String, Value>,
}

impl RecordAssertionBuilder {
    fn build(&mut self, valid_times: Range<zx::Time>) -> RecordAssertion {
        RecordAssertion {
            valid_times,
            severity: self.severity,
            arguments: std::mem::replace(&mut self.arguments, Default::default()),
        }
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
