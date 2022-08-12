// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::Error;
use diagnostics_reader::{ArchiveReader, Logs};
use fidl_fuchsia_diagnostics::Severity;
use fidl_fuchsia_diagnostics_stream::Record;
use fidl_fuchsia_validate_logs::{
    LogSinkPuppetMarker, LogSinkPuppetProxy, PrintfRecordSpec, PrintfValue, PuppetInfo, RecordSpec,
};
use fuchsia_async::Task;
use fuchsia_component::client::connect_to_protocol;
use futures::prelude::*;
use tracing::*;

#[fuchsia::main]
async fn main() -> Result<(), Error> {
    Puppet::launch().await?.test().await
}

struct Puppet {
    _info: PuppetInfo,
    _proxy: LogSinkPuppetProxy,
}

impl Puppet {
    async fn launch() -> Result<Self, Error> {
        info!("Connecting to puppet");
        let proxy = connect_to_protocol::<LogSinkPuppetMarker>()?;

        info!("Requesting info from the puppet.");
        let info = proxy.get_info().await?;

        Ok(Self { _proxy: proxy, _info: info })
    }

    async fn test(&self) -> Result<(), Error> {
        let test_log = "Test printf int %i string %s double %f";
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
        let record = RecordSpec {
            file: "test_file.cc".to_string(),
            line: 9001,
            record: Record { arguments: vec![], severity: Severity::Info, timestamp: 0 },
        };
        let mut spec: PrintfRecordSpec = PrintfRecordSpec {
            record,
            printf_arguments: vec![
                PrintfValue::IntegerValue(5),
                PrintfValue::StringValue("test".to_string()),
                PrintfValue::FloatValue(0.5),
            ],
            msg: "Test printf int %i string %s double %f".to_string(),
        };
        self._proxy.emit_printf_log(&mut spec).await?;
        info!("Got init message -- waiting for printf");

        loop {
            let mut log_entry = logs.next().await.unwrap();
            let has_msg =
                log_entry.payload_printf_format().map(|v| v.contains(test_log)).unwrap_or_default();
            if has_msg {
                // TODO (http://fxbug.dev/77054): Use our custom domain specific language once support for arrays is added.
                assert_eq!(log_entry.payload_printf_format(), Some(test_log));
                assert_eq!(
                    *log_entry.payload_printf_args().unwrap(),
                    vec!["5".to_string(), "test".to_string(), "0.5".to_string()]
                );
                break;
            }
        }
        info!("Tested structured printf successfully.");
        Ok(())
    }
}
