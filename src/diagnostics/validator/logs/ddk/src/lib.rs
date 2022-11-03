// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::Error;
use diagnostics_log_encoding::{Argument, Record, Severity, Value};
use diagnostics_reader::{ArchiveReader, Logs, SubscriptionResultsStream};
use fidl_fuchsia_device::ControllerProxy;
use fidl_fuchsia_validate_logs::{LogSinkPuppetProxy, PuppetInfo, RecordSpec};
use fuchsia_async::Task;
use futures::StreamExt;
use tracing::*;

struct Puppet {
    _info: PuppetInfo,
    proxy: LogSinkPuppetProxy,
    device_proxy: ControllerProxy,
    _reader_errors_task: Task<()>,
    logs: SubscriptionResultsStream<Logs>,
}

impl Puppet {
    // Creates a Puppet instance.
    // Since this is v2, there is no URL to spawn as we are using RealmBuilder.
    async fn launch(
        proxy: fidl_fuchsia_validate_logs::LogSinkPuppetProxy,
        device_proxy: ControllerProxy,
    ) -> Result<Self, Error> {
        info!("Requesting info from the puppet.");
        let info = proxy.get_info().await?;
        let reader = ArchiveReader::new();
        let (logs, mut errors) = reader.snapshot_then_subscribe::<Logs>().unwrap().split_streams();
        let task = Task::spawn(async move {
            while let Some(e) = errors.next().await {
                panic!("error in subscription: {}", e);
            }
        });
        Ok(Self { proxy, _info: info, _reader_errors_task: task, logs, device_proxy })
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

    async fn test_severity(&mut self) -> Result<(), Error> {
        let test_file = "test_file.cc".to_string();
        let test_line_32 = 9001;
        let long_test_log = "test_log_trace";
        let long_test_log_valid = "test_log_valid";
        let record = Record {
            arguments: vec![Argument {
                name: "message".to_string(),
                value: Value::Text(long_test_log.to_string()),
            }],
            severity: Severity::Trace,
            timestamp: 0,
        };
        let mut spec = RecordSpec { file: test_file.clone(), line: test_line_32, record };
        self.proxy.emit_log(&mut spec).await?;

        let _ = self
            .device_proxy
            .set_min_driver_log_severity(fidl_fuchsia_logger::LogLevelFilter::Trace)
            .await
            .unwrap();

        let record = Record {
            arguments: vec![Argument {
                name: "message".to_string(),
                value: Value::Text(long_test_log_valid.to_string()),
            }],
            severity: Severity::Trace,
            timestamp: 0,
        };
        let mut spec = RecordSpec { file: test_file, line: test_line_32, record };
        self.proxy.emit_log(&mut spec).await?;
        info!("Sent message");
        loop {
            let log_entry = self.logs.next().await.unwrap();
            if log_entry.msg().unwrap().contains(&long_test_log) {
                panic!("Unexpected trace log when trace logs were disabled.");
            }
            if log_entry.msg().unwrap().contains(&long_test_log_valid) {
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
        self.test_severity().await?;
        Ok(())
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    use {
        fidl::endpoints::Proxy,
        fidl_fuchsia_driver_test as fdt,
        fuchsia_component_test::RealmBuilder,
        fuchsia_driver_test::{DriverTestRealmBuilder, DriverTestRealmInstance},
    };

    #[fuchsia::test]
    async fn log_test() {
        let realm = RealmBuilder::new().await.unwrap();
        let _ = realm.driver_test_realm_setup().await.unwrap();
        let realm = realm.build().await.expect("failed to build realm");
        realm.driver_test_realm_start(fdt::RealmArgs::EMPTY).await.unwrap();
        let out_dir = realm.driver_test_realm_connect_to_dev().unwrap();

        let driver_service =
            device_watcher::recursive_wait_and_open_node(&out_dir, "sys/test/virtual-logsink")
                .await
                .unwrap();
        let driver_proxy = fidl_fuchsia_validate_logs::LogSinkPuppetProxy::from_channel(
            driver_service.into_channel().unwrap(),
        );
        let driver_service =
            device_watcher::recursive_wait_and_open_node(&out_dir, "sys/test/virtual-logsink")
                .await
                .unwrap();
        let device_proxy = ControllerProxy::from_channel(driver_service.into_channel().unwrap());
        let mut puppet = Puppet::launch(driver_proxy, device_proxy).await.unwrap();
        puppet.test().await.unwrap();
    }
}
