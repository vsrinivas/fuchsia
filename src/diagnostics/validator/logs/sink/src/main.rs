// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::Error;
use argh::FromArgs;
use diagnostics_stream::{parse::parse_record, Record, Severity, Value};
use fidl_fuchsia_logger::LogSinkRequest;
use fidl_fuchsia_logger::LogSinkRequestStream;
use fidl_fuchsia_sys::EnvironmentControllerProxy;
use fuchsia_async::Socket;
use fuchsia_component::client::App;
use fuchsia_component::server::ServiceFs;
use fuchsia_zircon as zx;
use futures::prelude::*;
use std::{
    collections::{BTreeMap, BTreeSet},
    ops::Range,
};
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
    url: String,
    start_time: zx::Time,
    socket: Socket,
    _app: App,
    _env: EnvironmentControllerProxy,
}

impl Puppet {
    async fn launch(puppet_url: &str) -> Result<Self, Error> {
        let mut fs = ServiceFs::new();
        fs.add_fidl_service(|s: LogSinkRequestStream| s);

        let start_time = zx::Time::get_monotonic();
        info!(%puppet_url, "launching");
        let (_env, _app) = fs
            .launch_component_in_nested_environment(
                puppet_url.to_owned(),
                None,
                "log_validator_puppet",
            )
            .unwrap();

        fs.take_and_serve_directory_handle()?;
        info!("Waiting for LogSink connection.");
        let mut stream = fs.next().await.unwrap();

        info!("Waiting for LogSink.Connect call.");
        if let LogSinkRequest::ConnectStructured { socket, control_handle: _ } =
            stream.next().await.unwrap()?
        {
            info!("Received a structured socket.");
            // TODO(fxbug.dev/61495): Validate that this is in fact a datagram socket.
            let socket = Socket::from_socket(socket)?;
            Ok(Self { socket, url: puppet_url.to_string(), start_time, _app, _env })
        } else {
            Err(anyhow::format_err!("shouldn't ever receive legacy connections"))
        }
    }

    // TODO(fxbug.dev/61538) validate we can log arbitrary messages
    async fn test(&self) -> Result<(), Error> {
        info!("Running the LogSink socket test.");

        let mut buf: Vec<u8> = vec![];
        let bytes_read = self.socket.read_datagram(&mut buf).await.unwrap();
        let actual = TestRecord::parse(&buf[0..bytes_read])?;
        let moniker = self.url.rsplit('/').next().unwrap();
        let expected =
            RecordAssertion::new(self.start_time..zx::Time::get_monotonic(), Severity::Warn)
                .add_tag(moniker)
                .add_tag("test_log")
                .add_string("foo", "bar")
                .build();
        assert_eq!(actual, expected);

        info!("Tested LogSink socket successfully.");
        Ok(())
    }
}

#[derive(Debug, PartialEq)]
struct TestRecord {
    timestamp: i64,
    severity: Severity,
    tags: BTreeSet<String>,
    arguments: BTreeMap<String, Value>,
}

impl TestRecord {
    fn parse(buf: &[u8]) -> Result<Self, Error> {
        let Record { timestamp, severity, arguments } = parse_record(buf)?.0;

        let mut tags = BTreeSet::new();
        let mut sorted_args = BTreeMap::new();
        for diagnostics_stream::Argument { name, value } in arguments {
            if name == "tag" {
                match value {
                    Value::Text(t) => {
                        tags.insert(t);
                    }
                    _ => anyhow::bail!("found non-text tag"),
                }
            } else if
            // TODO(adamperry): remove this hack in follow up which gets pid/tid from puppet
            name != "pid" && name != "tid" {
                sorted_args.insert(name, value);
            }
        }

        Ok(Self { timestamp, severity, tags, arguments: sorted_args })
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
    tags: BTreeSet<String>,
    arguments: BTreeMap<String, Value>,
}

impl RecordAssertion {
    fn new(valid_times: Range<zx::Time>, severity: Severity) -> RecordAssertionBuilder {
        RecordAssertionBuilder {
            valid_times,
            severity,
            tags: BTreeSet::new(),
            arguments: BTreeMap::new(),
        }
    }
}

impl PartialEq<TestRecord> for RecordAssertion {
    fn eq(&self, rhs: &TestRecord) -> bool {
        self.valid_times.contains(&zx::Time::from_nanos(rhs.timestamp))
            && self.severity == rhs.severity
            && self.tags == rhs.tags
            && self.arguments == rhs.arguments
    }
}

struct RecordAssertionBuilder {
    valid_times: Range<zx::Time>,
    severity: Severity,
    tags: BTreeSet<String>,
    arguments: BTreeMap<String, Value>,
}

impl RecordAssertionBuilder {
    fn build(&mut self) -> RecordAssertion {
        RecordAssertion {
            valid_times: self.valid_times.clone(),
            severity: self.severity,
            tags: std::mem::replace(&mut self.tags, Default::default()),
            arguments: std::mem::replace(&mut self.arguments, Default::default()),
        }
    }

    fn add_tag(&mut self, name: &str) -> &mut Self {
        self.tags.insert(name.to_owned());
        self
    }

    fn add_string(&mut self, name: &str, value: &str) -> &mut Self {
        self.arguments.insert(name.to_owned(), Value::Text(value.to_owned()));
        self
    }
}
