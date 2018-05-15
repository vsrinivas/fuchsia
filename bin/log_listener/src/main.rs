// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![deny(warnings)]

extern crate failure;
extern crate fidl;
extern crate fuchsia_app as app;
extern crate fuchsia_async as async;
extern crate fuchsia_zircon as zx;
extern crate futures;

use app::client::connect_to_service;
use failure::{Error, ResultExt};
use fidl::encoding2::OutOfLine;
use futures::future::ok as fok;
use std::collections::HashMap;
use std::env;
use std::io::{stdout, Write};

// Include the generated FIDL bindings for the `Logger` service.
extern crate fidl_logger;
use fidl_logger::{LogFilterOptions, LogLevelFilter, LogListener, LogListenerImpl,
                  LogListenerMarker, LogMarker, LogMessage, MAX_TAGS, MAX_TAG_LEN};

fn default_log_filter_options() -> LogFilterOptions {
    LogFilterOptions {
        filter_by_pid: false,
        pid: 0,
        min_severity: LogLevelFilter::None,
        verbosity: 0,
        filter_by_tid: false,
        tid: 0,
        tags: vec![],
    }
}

fn help(name: &str) -> String {
    format!(
        r"Usage: {} [flags]
        Flags:
        --tag <string>:
            Tag to filter on. Multiple tags can be specified by using multiple --tag flags.
            All the logs containing at least one of the passed tags would be printed.

        --pid <integer>:
            pid for the program to filter on.

        --tid <integer>:
            tid for the program to filter on.

        --severity <INFO|WARN|ERROR|FATAL>:
            Minimum severity to filter on.

        --verbosity <integer>:
            Verbosity to filer on. It should be positive integer greater than 0.

        --help | -h:
            Prints usage.",
        name
    )
}

fn parse_flags(args: &[String]) -> Result<LogFilterOptions, String> {
    if args.len() % 2 != 0 {
        return Err(String::from("Invalid args."));
    }
    let mut options = default_log_filter_options();

    let mut i = 0;
    while i < args.len() {
        let argument = &args[i];
        if args[i + 1].starts_with("-") {
            return Err(format!(
                "Invalid args. Pass argument after flag '{}'",
                argument
            ));
        }
        match argument.as_ref() {
            "--tag" => {
                let tag = &args[i + 1];
                if tag.len() > MAX_TAG_LEN as usize {
                    return Err(format!(
                        "'{}' should not be more then {} characters",
                        tag, MAX_TAG_LEN
                    ));
                }
                options.tags.push(String::from(tag.as_ref()));
                if options.tags.len() > MAX_TAGS as usize {
                    return Err(format!("Max tags allowed: {}", MAX_TAGS));
                }
            }
            "--severity" => match args[i + 1].as_ref() {
                "INFO" => options.min_severity = LogLevelFilter::Info,
                "WARN" => options.min_severity = LogLevelFilter::Warn,
                "ERROR" => options.min_severity = LogLevelFilter::Error,
                "FATAL" => options.min_severity = LogLevelFilter::Fatal,
                a => return Err(format!("Invalid severity: {}", a)),
            },
            "--verbosity" => if let Ok(v) = args[i + 1].parse::<u8>() {
                if v == 0 {
                    return Err(format!(
                        "Invalid verbosity: '{}', should be positive integer greater than 0.",
                        args[i + 1]
                    ));
                }
                options.verbosity = v;
            } else {
                return Err(format!(
                    "Invalid verbosity: '{}', should be positive integer greater than 0.",
                    args[i + 1]
                ));
            },
            "--pid" => {
                options.filter_by_pid = true;
                match args[i + 1].parse::<u64>() {
                    Ok(pid) => {
                        options.pid = pid;
                    }
                    Err(_) => {
                        return Err(format!(
                            "Invalid pid: '{}', should be a positive integer.",
                            args[i + 1]
                        ));
                    }
                }
            }
            "--tid" => {
                options.filter_by_tid = true;
                match args[i + 1].parse::<u64>() {
                    Ok(tid) => {
                        options.tid = tid;
                    }
                    Err(_) => {
                        return Err(format!(
                            "Invalid tid: '{}', should be a positive integer.",
                            args[i + 1]
                        ));
                    }
                }
            }
            a => {
                return Err(format!("Invalid option {}", a));
            }
        }
        i = i + 2;
    }
    return Ok(options);
}

struct Listener<W: Write + Send> {
    // stores pid, dropped_logs
    dropped_logs: HashMap<u64, u32>,
    writer: W,
}

impl<W> Listener<W>
where
    W: Write + Send,
{
    fn log(&mut self, message: &LogMessage) {
        let tags = message.tags.join(", ");
        writeln!(
            self.writer,
            "[{:05}.{:06}][{}][{}][{}] {}: {}",
            message.time / 1000000000,
            (message.time / 1000) % 1000000,
            message.pid,
            message.tid,
            tags,
            get_log_level(message.severity),
            message.msg
        ).expect("should not fail");
        if message.dropped_logs > 0
            && self.dropped_logs
                .get(&message.pid)
                .map(|d| d < &message.dropped_logs)
                .unwrap_or(true)
        {
            writeln!(
                self.writer,
                "[{:05}.{:06}][{}][{}][{}] WARNING: Dropped logs count: {}",
                message.time / 1000000000,
                (message.time / 1000) % 1000000,
                message.pid,
                message.tid,
                tags,
                message.dropped_logs
            ).expect("should not fail");
            self.dropped_logs.insert(message.pid, message.dropped_logs);
        }
    }
}

fn get_log_level(level: i32) -> String {
    match level {
        0 => "INFO".to_string(),
        1 => "WARNING".to_string(),
        2 => "ERROR".to_string(),
        3 => "FATAL".to_string(),
        l => {
            if l > 3 {
                "INVALID".to_string()
            } else {
                format!("VLOG({})", -l)
            }
        }
    }
}

fn log_listener<W>(listener: Listener<W>) -> impl LogListener
where
    W: Send + Write,
{
    LogListenerImpl {
        state: listener,
        on_open: |_,_| fok(()),
        done: |_, _| {
            //ignore, only called when dump_logs is called.
            fok(())
        },
        log: |listener, message, _| {
            listener.log(&message);
            fok(())
        },
        log_many: |listener, messages, _| {
            for msg in messages {
                listener.log(&msg);
            }
            fok(())
        },
    }
}

fn run_log_listener(options: Option<&mut LogFilterOptions>) -> Result<(), Error> {
    let mut executor = async::Executor::new().context("Error creating executor")?;
    let logger = connect_to_service::<LogMarker>()?;
    let (log_listener_local, log_listener_remote) = zx::Channel::create()?;
    let log_listener_local = async::Channel::from_channel(log_listener_local)?;
    let listener_ptr =
        fidl::endpoints2::ClientEnd::<LogListenerMarker>::new(log_listener_remote);

    let options = options.map(OutOfLine);
    logger
        .listen(listener_ptr, options)
        .context("failed to register listener")?;

    let l = Listener {
        dropped_logs: HashMap::new(),
        writer: stdout(),
    };

    let listener_fut = log_listener(l).serve(log_listener_local);
    executor
        .run_singlethreaded(listener_fut)
        .map_err(Into::into)
}

fn main() {
    let args: Vec<String> = env::args().collect();
    if args.len() > 1 && (args[1] == "--help" || args[1] == "-h") {
        println!("{}\n", help(args[0].as_ref()));
        return;
    }
    let mut options = match parse_flags(&args[1..]) {
        Err(e) => {
            eprintln!("{}\n{}\n", e, help(args[0].as_ref()));
            return;
        }
        Ok(o) => o,
    };

    if let Err(e) = run_log_listener(Some(&mut options)) {
        eprintln!("LogListener: Error: {:?}", e);
    }
}

#[cfg(test)]
mod tests {
    extern crate tempdir;

    use self::tempdir::TempDir;
    use super::*;

    use std::fs::File;
    use std::io::Read;

    #[test]
    fn test_log_fn() {
        let _executor = async::Executor::new().expect("unable to create executor");
        let tmp_dir = TempDir::new("test").expect("should have created tempdir");
        let file_path = tmp_dir.path().join("tmp_file");
        let tmp_file = File::create(&file_path).expect("should have created file");
        let mut l = Listener {
            dropped_logs: HashMap::new(),
            writer: tmp_file,
        };

        // test log levels
        let mut message = LogMessage {
            pid: 123,
            tid: 321,
            severity: 0,
            time: 76352234564,
            msg: "hello".to_string(),
            dropped_logs: 0,
            tags: vec![],
        };
        l.log(&message);

        for level in vec![1, 2, 3, 4, 11, -1, -3] {
            message.severity = level;
            l.log(&message);
        }
        let mut expected = "".to_string();
        for level in &[
            "INFO", "WARNING", "ERROR", "FATAL", "INVALID", "INVALID", "VLOG(1)", "VLOG(3)",
        ] {
            expected.push_str(&format!("[00076.352234][123][321][] {}: hello\n", level));
        }

        // test tags
        message.severity = 0;
        message.tags = vec!["tag1".to_string()];
        l.log(&message);
        expected.push_str("[00076.352234][123][321][tag1] INFO: hello\n");

        message.tags.push("tag2".to_string());
        l.log(&message);
        expected.push_str("[00076.352234][123][321][tag1, tag2] INFO: hello\n");

        // test time
        message.time = 636253000631621;
        l.log(&message);
        let s = "[636253.000631][123][321][tag1, tag2] INFO: hello\n";
        expected.push_str(s);

        // test dropped logs
        message.dropped_logs = 1;
        l.log(&message);
        expected.push_str(s);
        expected.push_str("[636253.000631][123][321][tag1, tag2] WARNING: Dropped logs count: 1\n");
        l.log(&message);
        // will not print log count again
        expected.push_str(s);

        // change pid and test
        message.pid = 1234;
        l.log(&message);
        expected.push_str("[636253.000631][1234][321][tag1, tag2] INFO: hello\n");
        expected
            .push_str("[636253.000631][1234][321][tag1, tag2] WARNING: Dropped logs count: 1\n");

        // switch back pid and test
        message.pid = 123;
        l.log(&message);
        expected.push_str(s);
        message.dropped_logs = 2;
        l.log(&message);
        expected.push_str(s);
        expected.push_str("[636253.000631][123][321][tag1, tag2] WARNING: Dropped logs count: 2\n");

        let mut tmp_file = File::open(&file_path).expect("should have opened the file");
        let mut content = String::new();
        tmp_file
            .read_to_string(&mut content)
            .expect("something went wrong reading the file");

        assert_eq!(content, expected);
    }

    mod parse_flags {
        use super::*;

        fn parse_flag_test_helper(args: &[String], options: Option<&LogFilterOptions>) {
            match parse_flags(args) {
                Ok(l) => match options {
                    None => {
                        panic!("parse_flags should have returned error, got: {:?}", l);
                    }
                    Some(options) => {
                        assert_eq!(&l, options);
                    }
                },
                Err(e) => {
                    if let Some(_) = options {
                        panic!("did not expect error: {}", e);
                    }
                }
            }
        }

        #[test]
        fn invalid_options() {
            let args = vec!["--tag".to_string()];
            parse_flag_test_helper(&args, None);
        }

        #[test]
        fn invalid_options2() {
            let args = vec!["--tag".to_string(), "--severity".to_string()];
            parse_flag_test_helper(&args, None);
        }

        #[test]
        fn invalid_flag() {
            let args = vec![
                "--tag".to_string(),
                "tag".to_string(),
                "--invalid".to_string(),
            ];
            parse_flag_test_helper(&args, None);
        }

        #[test]
        fn one_tag() {
            let args = vec!["--tag".to_string(), "tag".to_string()];
            let mut expected = default_log_filter_options();
            expected.tags.push("tag".to_string());
            parse_flag_test_helper(&args, Some(&expected));
        }

        #[test]
        fn multiple_tags() {
            let args = vec![
                "--tag".to_string(),
                "tag".to_string(),
                "--tag".to_string(),
                "tag1".to_string(),
            ];
            let mut expected = default_log_filter_options();
            expected.tags.push("tag".to_string());
            expected.tags.push("tag1".to_string());
            parse_flag_test_helper(&args, Some(&expected));
        }

        #[test]
        fn pid() {
            let args = vec!["--pid".to_string(), "123".to_string()];
            let mut expected = default_log_filter_options();
            expected.filter_by_pid = true;
            expected.pid = 123;
            parse_flag_test_helper(&args, Some(&expected));
        }

        #[test]
        fn pid_fail() {
            let args = vec!["--pid".to_string(), "123a".to_string()];
            parse_flag_test_helper(&args, None);
        }

        #[test]
        fn tid() {
            let args = vec!["--tid".to_string(), "123".to_string()];
            let mut expected = default_log_filter_options();
            expected.filter_by_tid = true;
            expected.tid = 123;
            parse_flag_test_helper(&args, Some(&expected));
        }

        #[test]
        fn tid_fail() {
            let args = vec!["--tid".to_string(), "123a".to_string()];
            parse_flag_test_helper(&args, None);
        }

        #[test]
        fn severity() {
            let mut expected = default_log_filter_options();
            expected.min_severity = LogLevelFilter::None;
            for s in vec!["INFO", "WARN", "ERROR", "FATAL"] {
                let mut args = vec!["--severity".to_string(), s.to_string()];
                expected.min_severity = LogLevelFilter::from_primitive(
                    expected.min_severity.into_primitive() + 1,
                ).unwrap();
                parse_flag_test_helper(&args, Some(&expected));
            }
        }

        #[test]
        fn severity_fail() {
            let args = vec!["--severity".to_string(), "DEBUG".to_string()];
            parse_flag_test_helper(&args, None);
        }

        #[test]
        fn verbosity() {
            let args = vec!["--verbosity".to_string(), "2".to_string()];
            let mut expected = default_log_filter_options();
            expected.verbosity = 2;
            parse_flag_test_helper(&args, Some(&expected));
        }

        #[test]
        fn verbosity_fail() {
            let mut args = vec!["--verbosity".to_string(), "-2".to_string()];
            parse_flag_test_helper(&args, None);

            args[1] = "str".to_string();
            parse_flag_test_helper(&args, None);

            args[1] = "0".to_string();
            parse_flag_test_helper(&args, None);
        }

        #[test]
        fn tag_edge_case() {
            let mut args = vec!["--tag".to_string()];
            let mut tag = "a".to_string();
            for _ in 1..MAX_TAG_LEN {
                tag.push('a');
            }
            args.push(tag.clone());
            let mut expected = default_log_filter_options();
            expected.tags.push(tag);
            parse_flag_test_helper(&args, Some(&expected));

            args[1] = "tag1".to_string();
            expected.tags[0] = args[1].clone();
            for i in 1..MAX_TAGS {
                args.push("--tag".to_string());
                args.push(format!("tag{}", i));
                expected.tags.push(format!("tag{}", i));
            }
            parse_flag_test_helper(&args, Some(&expected));
        }

        #[test]
        fn tag_fail() {
            let mut args = vec!["--tag".to_string()];
            let mut tag = "a".to_string();
            for _ in 0..MAX_TAG_LEN {
                tag.push('a');
            }
            args.push(tag);
            parse_flag_test_helper(&args, None);

            args[1] = "tag1".to_string();
            for i in 0..MAX_TAGS {
                args.push("--tag".to_string());
                args.push(format!("tag{}", i));
            }
            parse_flag_test_helper(&args, None);
        }
    }
}
