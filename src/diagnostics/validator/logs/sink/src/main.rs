// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::Error;
use argh::FromArgs;
use diagnostics_stream::{parse::parse_record, Record, Severity, Value};
use fidl_fuchsia_logger::LogSinkRequest;
use fidl_fuchsia_logger::LogSinkRequestStream;
use fidl_fuchsia_sys::EnvironmentControllerProxy;
use fidl_fuchsia_validate_logs::{LogSinkPuppetMarker, LogSinkPuppetProxy, PuppetInfo, RecordSpec};
use fuchsia_async::{Socket, Task};
use fuchsia_component::server::ServiceFs;
use fuchsia_zircon as zx;
use futures::prelude::*;
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
            info!("Received a structured socket.");
            assert!(
                socket.info()?.options.contains(zx::SocketOpts::DATAGRAM),
                "log sockets must be created in datagram mode"
            );

            Ok(Self {
                socket: Socket::from_socket(socket)?,
                proxy,
                info,
                start_time,
                _app_watchdog,
                _env,
            })
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
        info!("Ensuring we received the init message.");

        assert_eq!(
            self.read_record().await?,
            RecordAssertion::new(
                &self.info,
                self.start_time..zx::Time::get_monotonic(),
                Severity::Info
            )
            .add_string("message", "Puppet started.")
            .build()
        );

        info!("Starting the LogSink socket test.");

        let before = zx::Time::get_monotonic();
        // TODO(fxbug.dev/61538) validate we can log arbitrary messages
        let record = fidl_fuchsia_diagnostics_stream::Record {
            arguments: vec![
                fidl_fuchsia_diagnostics_stream::Argument {
                    name: "message".to_string(),
                    value: diagnostics_stream::Value::Text("test_log".to_string()),
                },
                fidl_fuchsia_diagnostics_stream::Argument {
                    name: "foo".to_string(),
                    value: diagnostics_stream::Value::Text("barstool".to_string()),
                },
            ],
            severity: fidl_fuchsia_diagnostics::Severity::Warn,
            timestamp: 0,
        };
        let mut spec = RecordSpec { file: "test".to_string(), line: 25, record: record };
        self.proxy.emit_log(&mut spec).await?;
        let after = zx::Time::get_monotonic();
        assert_eq!(
            self.read_record().await?,
            RecordAssertion::new(&self.info, before..after, Severity::Warn)
                .add_string("message", "test_log")
                .add_string("foo", "barstool")
                .build()
        );

        info!("Tested LogSink socket successfully.");
        Ok(())
    }
}

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
    fn new(
        info: &PuppetInfo,
        valid_times: Range<zx::Time>,
        severity: Severity,
    ) -> RecordAssertionBuilder {
        let mut builder =
            RecordAssertionBuilder { valid_times, severity, arguments: BTreeMap::new() };

        builder.add_unsigned("pid", info.pid);
        builder.add_unsigned("tid", info.tid);

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
    valid_times: Range<zx::Time>,
    severity: Severity,
    arguments: BTreeMap<String, Value>,
}

impl RecordAssertionBuilder {
    fn build(&mut self) -> RecordAssertion {
        RecordAssertion {
            valid_times: self.valid_times.clone(),
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
}
