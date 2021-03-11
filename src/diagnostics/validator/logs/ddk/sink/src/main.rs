// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::Error;
use argh::FromArgs;
use diagnostics_log_encoding::Value;
use diagnostics_reader::{ArchiveReader, Logs};
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

#[fuchsia_async::run_singlethreaded]
async fn main() -> Result<(), Error> {
    fuchsia_syslog::init_with_tags(&[]).unwrap();
    let Opt { puppet_url } = argh::from_env();
    Puppet::launch(&puppet_url).await?.test().await
}

struct Puppet {
    _info: PuppetInfo,
    _proxy: LogSinkPuppetProxy,
    _env: App,
}

impl Puppet {
    async fn launch(puppet_url: &str) -> Result<Self, Error> {
        let local_launcher = launcher()?;
        let app = launch(&local_launcher, puppet_url.to_string(), None)?;
        info!("Connecting to puppet and spawning watchdog.");
        let proxy = app.connect_to_service::<LogSinkPuppetMarker>()?;

        info!("Requesting info from the puppet.");
        let info = proxy.get_info().await?;

        Ok(Self { _proxy: proxy, _info: info, _env: app })
    }

    async fn test(&self) -> Result<(), Error> {
        let test_log = "test_log";
        let test_file = "test_file.cc".to_string();
        let test_line_64: u64 = 9001;
        let test_line_32 = 9001;
        info!("Ensuring we received the init message.");
        let reader = ArchiveReader::new();
        let (mut logs, mut errors) =
            reader.snapshot_then_subscribe::<Logs>().unwrap().split_streams();
        let _errors = Task::spawn(async move {
            while let Some(e) = errors.next().await {
                panic!("error in subscription: {}", e);
            }
        });
        loop {
            let log_entry = logs.next().await.unwrap();
            if log_entry.msg().unwrap().contains("Puppet started.") {
                break;
            }
        }
        info!("Got init message -- sending custom message");
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
        self._proxy.emit_log(&mut spec).await?;
        info!("Sent message");
        loop {
            let log_entry = logs.next().await.unwrap();
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
}
