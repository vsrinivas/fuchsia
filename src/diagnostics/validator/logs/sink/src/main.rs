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
use futures::prelude::*;
use std::collections::{BTreeMap, BTreeSet};
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
    socket: Socket,
    _app: App,
    _env: EnvironmentControllerProxy,
}

impl Puppet {
    async fn launch(puppet_url: &str) -> Result<Self, Error> {
        let mut fs = ServiceFs::new();
        fs.add_fidl_service(|s: LogSinkRequestStream| s);

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
            let socket = Socket::from_socket(socket)?;
            Ok(Self { socket, url: puppet_url.to_string(), _app, _env })
        } else {
            Err(anyhow::format_err!("shouldn't ever receive legacy connections"))
        }
    }

    async fn test(&self) -> Result<(), Error> {
        info!("Running the LogSink socket test.");

        let mut buf: Vec<u8> = vec![];
        // TODO(fxbug.dev/61495): Validate that this is in fact a datagram socket.
        let bytes_read = self.socket.read_datagram(&mut buf).await.unwrap();
        let actual = TestRecord::parse(&buf[0..bytes_read])?;

        // TODO(fxbug.dev/61538) validate we can log arbitrary messages
        let moniker = self.url.rsplit('/').next().unwrap();
        let expected = TestRecord::new(actual.timestamp, Severity::Warn)
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
    fn new(timestamp: i64, severity: Severity) -> TestRecordBuilder {
        TestRecordBuilder { timestamp, severity, tags: BTreeSet::new(), arguments: BTreeMap::new() }
    }

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

struct TestRecordBuilder {
    timestamp: i64,
    severity: Severity,
    tags: BTreeSet<String>,
    arguments: BTreeMap<String, Value>,
}

impl TestRecordBuilder {
    fn build(&mut self) -> TestRecord {
        TestRecord {
            timestamp: self.timestamp,
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
