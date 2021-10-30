// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::Error;
use argh::FromArgs;
use diagnostics_log_encoding::Value;
use diagnostics_reader::{ArchiveReader, Logs, SubscriptionResultsStream};
use fidl_fuchsia_diagnostics::Severity;
use fidl_fuchsia_diagnostics_stream::{Argument, Record};
use fidl_fuchsia_validate_logs::{LogSinkPuppetMarker, LogSinkPuppetProxy, PuppetInfo, RecordSpec};
use fuchsia_async::Task;
use fuchsia_component::client::{launch, launcher, App};
use futures::prelude::*;
use tracing::*;

/// Validate Log VMO formats written by 'puppet' programs controlled by
/// this Validator program.
#[derive(Debug, FromArgs)]
struct Opt {
    /// required arg: The URL of the puppet
    #[argh(option, long = "url")]
    puppet_url: String,
}

#[fuchsia::component]
async fn main() -> Result<(), Error> {
    let Opt { puppet_url } = argh::from_env();
    Puppet::launch(&puppet_url).await?.test().await
}

struct Puppet {
    _info: PuppetInfo,
    proxy: LogSinkPuppetProxy,
    _env: App,
    _reader_errors_task: Task<()>,
    logs: SubscriptionResultsStream<Logs>,
}

impl Puppet {
    async fn launch(puppet_url: &str) -> Result<Self, Error> {
        let local_launcher = launcher()?;
        let app = launch(&local_launcher, puppet_url.to_string(), None)?;
        info!("Connecting to puppet and spawning watchdog.");
        let proxy = app.connect_to_protocol::<LogSinkPuppetMarker>()?;

        info!("Requesting info from the puppet.");
        let info = proxy.get_info().await?;
        let reader = ArchiveReader::new();
        let (logs, mut errors) = reader.snapshot_then_subscribe::<Logs>().unwrap().split_streams();
        let task = Task::spawn(async move {
            while let Some(e) = errors.next().await {
                panic!("error in subscription: {}", e);
            }
        });
        Ok(Self { proxy, _info: info, _env: app, _reader_errors_task: task, logs })
    }

    async fn test_puppet_started(&mut self) -> Result<(), Error> {
        info!("Ensuring we received the init message.");

        loop {
            let log_entry = self.logs.next().await.unwrap();
            if log_entry.msg().unwrap().contains("Puppet started.") {
                break;
            }
        }
        info!("Got init message");
        Ok(())
    }

    async fn test_basic_log(&mut self) -> Result<(), Error> {
        let test_log = "test_log";
        let test_file = "test_file.cc".to_string();
        let test_line_64: u64 = 9001;
        let test_line_32 = 9001;
        // TODO(https://fxbug.dev/66981): Additional arguments aren't yet supported by the DDK.
        let record = Record {
            arguments: vec![Argument {
                name: "message".to_string(),
                value: Value::Text(test_log.to_string()),
            }],
            severity: Severity::Error,
            timestamp: 0,
        };
        let mut spec = RecordSpec { file: test_file.clone(), line: test_line_32, record };
        self.proxy.emit_log(&mut spec).await?;
        info!("Sent message");
        loop {
            let log_entry = self.logs.next().await.unwrap();
            let has_msg = log_entry.msg().unwrap().contains(test_log);
            let has_file = match log_entry.file_path() {
                None => false,
                Some(file) => file == test_file.clone(),
            };
            let has_line = match log_entry.line_number() {
                None => false,
                Some(line) => *line == test_line_64,
            };
            if has_msg && has_file && has_line {
                break;
            }
        }
        info!("Tested LogSink socket successfully.");
        Ok(())
    }

    async fn test_file_line(&mut self) -> Result<(), Error> {
        let test_file = "test_file.cc".to_string();
        let test_line_32 = 9001;
        let long_test_log = "test_log_".repeat(1000);
        let record = Record {
            arguments: vec![Argument {
                name: "message".to_string(),
                value: Value::Text(long_test_log.to_string()),
            }],
            severity: Severity::Info,
            timestamp: 0,
        };
        let mut spec = RecordSpec { file: test_file, line: test_line_32, record };
        self.proxy.emit_log(&mut spec).await?;
        info!("Sent message");
        loop {
            let log_entry = self.logs.next().await.unwrap();
            let has_msg = log_entry.msg().unwrap().contains(&"test_log_".repeat(110).to_owned());
            if has_msg {
                break;
            }
        }
        info!("Tested file/line number successfully.");
        Ok(())
    }

    async fn test(&mut self) -> Result<(), Error> {
        self.test_puppet_started().await?;
        self.test_basic_log().await?;
        self.test_file_line().await?;
        Ok(())
    }
}
