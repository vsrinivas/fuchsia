// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// dummy main. We do not copy this binary to fuchsia, only tests.
fn main() {}

#[cfg(test)]
mod tests {
    use fidl_fuchsia_logger::{LogFilterOptions, LogLevelFilter, LogMessage};
    use fuchsia_async as fasync;
    use fuchsia_syslog::{self as syslog, fx_log_info};
    use fuchsia_syslog_listener::{self as syslog_listener, LogProcessor};
    use fuchsia_zircon as zx;
    use log::warn;
    use parking_lot::Mutex;

    use std::sync::Arc;

    struct Listener {
        log_messages: Arc<Mutex<Vec<LogMessage>>>,
    }

    impl LogProcessor for Listener {
        fn log(&mut self, message: LogMessage) {
            self.log_messages.lock().push(message);
        }

        fn done(&mut self) {
            panic!("this should not be called");
        }
    }

    fn run_listener(tag: &str) -> Arc<Mutex<Vec<LogMessage>>> {
        let mut options = LogFilterOptions {
            filter_by_pid: false,
            pid: 0,
            min_severity: LogLevelFilter::None,
            verbosity: 0,
            filter_by_tid: false,
            tid: 0,
            tags: vec![tag.to_string()],
        };
        let logs = Arc::new(Mutex::new(Vec::new()));
        let l = Listener { log_messages: logs.clone() };
        fasync::spawn(async move {
            let fut = syslog_listener::run_log_listener(l, Some(&mut options), false);
            if let Err(e) = fut.await {
                panic!("test fail {:?}", e);
            }
        });
        return logs;
    }

    #[test]
    fn test_listen_for_syslog() {
        let mut executor = fasync::Executor::new().unwrap();
        let random = rand::random::<u16>();
        let tag = "logger_integration_rust".to_string() + &random.to_string();
        syslog::init_with_tags(&[&tag]).expect("should not fail");
        let logs = run_listener(&tag);
        fx_log_info!("my msg: {}", 10);
        warn!("log crate: {}", 20);

        loop {
            if logs.lock().len() >= 2 {
                break;
            }
            executor.run_one_step(&mut futures::future::pending::<()>());
        }
        let logs = logs.lock();
        assert_eq!(2, logs.len());
        assert_eq!(logs[0].tags, vec![tag.clone()]);
        assert_eq!(logs[0].severity, LogLevelFilter::Info as i32);
        assert_eq!(logs[0].msg, "my msg: 10");

        assert_eq!(logs[1].tags[0], tag.clone());
        assert_eq!(logs[1].severity, LogLevelFilter::Warn as i32);
        assert_eq!(logs[1].msg, "log crate: 20");
    }

    #[test]
    fn test_listen_for_klog() {
        let mut executor = fasync::Executor::new().unwrap();
        let logs = run_listener("klog");

        let msg = format!("logger_integration_rust test_klog {}", rand::random::<u64>());

        let resource = zx::Resource::from(zx::Handle::invalid());
        let debuglog = zx::DebugLog::create(&resource, zx::DebugLogOpts::empty()).unwrap();
        debuglog.write(msg.as_bytes()).unwrap();

        loop {
            let logs_lock = logs.lock();
            if logs_lock.iter().find(|&m| m.msg == msg).is_some() {
                return;
            }
            drop(logs_lock);
            executor.run_one_step(&mut futures::future::pending::<()>());
        }
    }
}
