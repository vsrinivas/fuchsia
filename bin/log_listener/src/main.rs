// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![deny(warnings)]

extern crate chrono;
use chrono::TimeZone;

extern crate failure;
extern crate fuchsia_async as async;
extern crate fuchsia_syslog_listener as syslog_listener;

extern crate fuchsia_zircon as zx;

use failure::{Error, ResultExt};
use std::collections::hash_set::HashSet;
use std::collections::HashMap;
use std::env;
use std::fs::OpenOptions;
use std::io::{stdout, Write};
use syslog_listener::LogProcessor;

// Include the generated FIDL bindings for the `Logger` service.
extern crate fidl_fuchsia_logger;
use fidl_fuchsia_logger::{
    LogFilterOptions, LogLevelFilter, LogMessage, MAX_TAGS, MAX_TAG_LEN_BYTES,
};

#[derive(Debug, PartialEq)]
struct LogListenerOptions {
    filter: LogFilterOptions,
    local: LocalOptions,
}

impl Default for LogListenerOptions {
    fn default() -> LogListenerOptions {
        LogListenerOptions {
            filter: LogFilterOptions {
                filter_by_pid: false,
                pid: 0,
                min_severity: LogLevelFilter::Info,
                verbosity: 0,
                filter_by_tid: false,
                tid: 0,
                tags: vec![],
            },
            local: LocalOptions::default(),
        }
    }
}

#[derive(Debug, PartialEq, Clone)]
struct LocalOptions {
    file: Option<String>,
    ignore_tags: HashSet<String>,
    clock: Clock,
    time_format: String,
}

impl Default for LocalOptions {
    fn default() -> LocalOptions {
        LocalOptions {
            file: None,
            ignore_tags: HashSet::new(),
            clock: Clock::Monotonic,
            time_format: "%Y-%m-%d %H:%M:%S".to_string(),
        }
    }
}

impl LocalOptions {
    fn format_time(&self, timestamp: u64) -> String {
        match self.clock {
            Clock::Monotonic => format!(
                "{:05}.{:06}",
                timestamp / 1000000000,
                (timestamp / 1000) % 1000000
            ),
            Clock::UTC => self
                ._monotonic_to_utc(timestamp)
                .format(&self.time_format)
                .to_string(),
            Clock::Local => chrono::Local
                .from_utc_datetime(&self._monotonic_to_utc(timestamp))
                .format(&self.time_format)
                .to_string(),
        }
    }

    fn _monotonic_to_utc(&self, timestamp: u64) -> chrono::NaiveDateTime {
        // Find UTC offset for Monotonic.
        // Must compute this every time since UTC time can be adjusted.
        // Note that when printing old messages from memory buffer then
        // this may offset them from UTC time as set when logged in
        // case of UTC time adjustments since.
        let monotonic_zero_as_utc =
            zx::Time::get(zx::ClockId::UTC).nanos() - zx::Time::get(zx::ClockId::Monotonic).nanos();
        let shifted_timestamp = monotonic_zero_as_utc + timestamp;
        let seconds = (shifted_timestamp / 1000000000) as i64;
        let nanos = (shifted_timestamp % 1000000000) as u32;
        chrono::NaiveDateTime::from_timestamp(seconds, nanos)
    }
}

#[derive(Debug, PartialEq, Clone)]
enum Clock {
    Monotonic, // Corresponds to ZX_CLOCK_MONOTONIC
    UTC,       // Corresponds to ZX_UTC_MONOTONIC
    Local,     // Localized wall time
}

fn help(name: &str) -> String {
    format!(
        r#"Usage: {} [flags]
        Flags:
        --tag <string>:
            Tag to filter on. Multiple tags can be specified by using multiple --tag flags.
            All the logs containing at least one of the passed tags would be printed.

        --ignore-tag <string>:
            Tag to ignore. Any logs containing at least one of the passed tags will not be
            printed.

        --pid <integer>:
            pid for the program to filter on.

        --tid <integer>:
            tid for the program to filter on.

        --severity <INFO|WARN|ERROR|FATAL>:
            Minimum severity to filter on.
            Defaults to INFO.

        --verbosity <integer>:
            Verbosity to filter on. It should be positive integer greater than 0.
            If this is passed, it overrides default severity.
            Errors out if both this and --severity are passed.
            Defaults to 0 which means don't filter on verbosity.

        --file <string>:
            File to write logs to. If omitted, logs are written to stdout.

        --clock <Monotonic|UTC|Local>:
            Select clock to use for timestamps.
            Monotonic (default): same as ZX_CLOCK_MONOTONIC.
            UTC: same as ZX_CLOCK_UTC.
            Local: localized wall time.

        --time_format <format>:
            If --clock is not MONOTONIC, specify timestamp format.
            See chrono::format::strftime for format specifiers.
            Defaults to "%Y-%m-%d %H:%M:%S".

        --help | -h:
            Prints usage."#,
        name
    )
}

fn parse_flags(args: &[String]) -> Result<LogListenerOptions, String> {
    if args.len() % 2 != 0 {
        return Err(String::from("Invalid args."));
    }
    let mut options = LogListenerOptions::default();

    let mut i = 0;
    let mut severity_passed = false;
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
                if tag.len() > MAX_TAG_LEN_BYTES as usize {
                    return Err(format!(
                        "'{}' should not be more than {} characters",
                        tag, MAX_TAG_LEN_BYTES
                    ));
                }
                options.filter.tags.push(String::from(tag.as_ref()));
                if options.filter.tags.len() > MAX_TAGS as usize {
                    return Err(format!("Max tags allowed: {}", MAX_TAGS));
                }
            }
            "--ignore-tag" => {
                let tag = &args[i + 1];
                if tag.len() > MAX_TAG_LEN_BYTES as usize {
                    return Err(format!(
                        "'{}' should not be more than {} characters",
                        tag, MAX_TAG_LEN_BYTES
                    ));
                }
                options.local.ignore_tags.insert(String::from(tag.as_ref()));
            }
            "--severity" => {
                if options.filter.verbosity > 0 {
                    return Err(
                        "Invalid arguments: Cannot pass both severity and verbosity".to_string()
                    );
                }
                severity_passed = true;
                match args[i + 1].as_ref() {
                    "INFO" => options.filter.min_severity = LogLevelFilter::Info,
                    "WARN" => options.filter.min_severity = LogLevelFilter::Warn,
                    "ERROR" => options.filter.min_severity = LogLevelFilter::Error,
                    "FATAL" => options.filter.min_severity = LogLevelFilter::Fatal,
                    a => return Err(format!("Invalid severity: {}", a)),
                }
            }
            "--verbosity" => if let Ok(v) = args[i + 1].parse::<u8>() {
                if severity_passed {
                    return Err(
                        "Invalid arguments: Cannot pass both severity and verbosity".to_string()
                    );
                }
                if v == 0 {
                    return Err(format!(
                        "Invalid verbosity: '{}', should be positive integer greater than 0.",
                        args[i + 1]
                    ));
                }
                options.filter.min_severity = LogLevelFilter::None;
                options.filter.verbosity = v;
            } else {
                return Err(format!(
                    "Invalid verbosity: '{}', should be positive integer greater than 0.",
                    args[i + 1]
                ));
            },
            "--pid" => {
                options.filter.filter_by_pid = true;
                match args[i + 1].parse::<u64>() {
                    Ok(pid) => {
                        options.filter.pid = pid;
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
                options.filter.filter_by_tid = true;
                match args[i + 1].parse::<u64>() {
                    Ok(tid) => {
                        options.filter.tid = tid;
                    }
                    Err(_) => {
                        return Err(format!(
                            "Invalid tid: '{}', should be a positive integer.",
                            args[i + 1]
                        ));
                    }
                }
            }
            "--file" => {
                options.local.file = Some((&args[i + 1]).clone());
            }
            "--clock" => match args[i + 1].to_lowercase().as_ref() {
                "monotonic" => options.local.clock = Clock::Monotonic,
                "utc" => options.local.clock = Clock::UTC,
                "local" => options.local.clock = Clock::Local,
                a => return Err(format!("Invalid clock: {}", a)),
            },
            "--time_format" => {
                options.local.time_format = args[i + 1].clone();
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
    local_options: LocalOptions,
    writer: W,
}

impl<W> LogProcessor for Listener<W>
where
    W: Write + Send,
{
    fn log(&mut self, message: LogMessage) {
        if message
            .tags
            .iter()
            .any(|tag| self.local_options.ignore_tags.contains(tag))
        {
            return;
        }
        let tags = message.tags.join(", ");
        writeln!(
            self.writer,
            "[{}][{}][{}][{}] {}: {}",
            self.local_options.format_time(message.time),
            message.pid,
            message.tid,
            tags,
            get_log_level(message.severity),
            message.msg
        ).expect("should not fail");
        if message.dropped_logs > 0
            && self
                .dropped_logs
                .get(&message.pid)
                .map(|d| d < &message.dropped_logs)
                .unwrap_or(true)
        {
            writeln!(
                self.writer,
                "[{}][{}][{}][{}] WARNING: Dropped logs count: {}",
                self.local_options.format_time(message.time),
                message.pid,
                message.tid,
                tags,
                message.dropped_logs
            ).expect("should not fail");
            self.dropped_logs.insert(message.pid, message.dropped_logs);
        }
    }

    fn done(&mut self) {
        // ignore as this is not called incase of listener.
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

fn new_listener(local_options: LocalOptions) -> Result<Listener<Box<dyn Write + Send>>, Error> {
    let writer: Box<dyn Write + Send> = match local_options.file {
        None => Box::new(stdout()),
        Some(ref name) => {
            let f = OpenOptions::new().append(true).create(true).open(name)?;
            Box::new(f)
        }
    };
    Ok(Listener {
        dropped_logs: HashMap::new(),
        writer: writer,
        local_options: local_options,
    })
}

fn run_log_listener(options: Option<&mut LogListenerOptions>) -> Result<(), Error> {
    let mut executor = async::Executor::new().context("Error creating executor")?;
    let (filter_options, local_options) = options.map_or_else(
        || (None, LocalOptions::default()),
        |o| (Some(&mut o.filter), o.local.clone()),
    );
    let l = new_listener(local_options)?;
    let listener_fut = syslog_listener::run_log_listener(l, filter_options, false)?;
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

    fn copy_log_message(msg: &LogMessage) -> LogMessage {
        LogMessage {
            pid: msg.pid,
            tid: msg.tid,
            severity: msg.severity,
            time: msg.time,
            msg: msg.msg.clone(),
            dropped_logs: msg.dropped_logs,
            tags: msg.tags.clone(),
        }
    }

    #[test]
    fn test_log_fn() {
        let _executor = async::Executor::new().expect("unable to create executor");
        let tmp_dir = TempDir::new("test").expect("should have created tempdir");
        let file_path = tmp_dir.path().join("tmp_file");
        let tmp_file = File::create(&file_path).expect("should have created file");
        let mut l = Listener {
            dropped_logs: HashMap::new(),
            writer: tmp_file,
            local_options: LocalOptions::default(),
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
        l.log(copy_log_message(&message));

        for level in vec![1, 2, 3, 4, 11, -1, -3] {
            message.severity = level;
            l.log(copy_log_message(&message));
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
        l.log(copy_log_message(&message));
        expected.push_str("[00076.352234][123][321][tag1] INFO: hello\n");

        message.tags.push("tag2".to_string());
        l.log(copy_log_message(&message));
        expected.push_str("[00076.352234][123][321][tag1, tag2] INFO: hello\n");

        // test Monotonic time
        message.time = 636253000631621;
        l.log(copy_log_message(&message));
        let s = "[636253.000631][123][321][tag1, tag2] INFO: hello\n";
        expected.push_str(s);

        // test dropped logs
        message.dropped_logs = 1;
        l.log(copy_log_message(&message));
        expected.push_str(s);
        expected.push_str("[636253.000631][123][321][tag1, tag2] WARNING: Dropped logs count: 1\n");
        l.log(copy_log_message(&message));
        // will not print log count again
        expected.push_str(s);

        // change pid and test
        message.pid = 1234;
        l.log(copy_log_message(&message));
        expected.push_str("[636253.000631][1234][321][tag1, tag2] INFO: hello\n");
        expected
            .push_str("[636253.000631][1234][321][tag1, tag2] WARNING: Dropped logs count: 1\n");

        // switch back pid and test
        message.pid = 123;
        l.log(copy_log_message(&message));
        expected.push_str(s);
        message.dropped_logs = 2;
        l.log(copy_log_message(&message));
        expected.push_str(s);
        expected.push_str("[636253.000631][123][321][tag1, tag2] WARNING: Dropped logs count: 2\n");

        let mut tmp_file = File::open(&file_path).expect("should have opened the file");
        let mut content = String::new();
        tmp_file
            .read_to_string(&mut content)
            .expect("something went wrong reading the file");

        assert_eq!(content, expected);
    }

    #[test]
    fn test_format_monotonic_time() {
        let mut local_options = LocalOptions::default();
        let timestamp = 636253000631621;

        let formatted = local_options.format_time(timestamp); // Test default
        assert_eq!(formatted, "636253.000631");
        local_options.clock = Clock::Monotonic;
        let formatted = local_options.format_time(timestamp);
        assert_eq!(formatted, "636253.000631");
    }

    #[test]
    fn test_format_utc_time() {
        let mut local_options = LocalOptions::default();
        let timestamp = 636253000631621;
        local_options.clock = Clock::UTC;
        local_options.time_format = "%H:%M:%S %d/%m/%Y".to_string();

        let timestamp_utc_formatted = local_options.format_time(timestamp);
        let timestamp_utc_struct = chrono::NaiveDateTime::parse_from_str(
            &timestamp_utc_formatted,
            &local_options.time_format,
        ).unwrap();
        assert_eq!(
            timestamp_utc_struct
                .format(&local_options.time_format)
                .to_string(),
            timestamp_utc_formatted
        );
        let zero_utc_formatted = local_options.format_time(0);
        assert_ne!(zero_utc_formatted, timestamp_utc_formatted);
    }

    mod parse_flags {
        use super::*;

        fn parse_flag_test_helper(args: &[String], options: Option<&LogListenerOptions>) {
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
            let mut expected = LogListenerOptions::default();
            expected.filter.tags.push("tag".to_string());
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
            let mut expected = LogListenerOptions::default();
            expected.filter.tags.push("tag".to_string());
            expected.filter.tags.push("tag1".to_string());
            parse_flag_test_helper(&args, Some(&expected));
        }

        #[test]
        fn one_ignore_tag() {
            let args = vec!["--ignore-tag".to_string(), "tag".to_string()];
            let mut expected = LogListenerOptions::default();
            expected.local.ignore_tags.insert("tag".to_string());
            parse_flag_test_helper(&args, Some(&expected));
        }

        #[test]
        fn multiple_ignore_tags() {
            let args = vec![
                "--ignore-tag".to_string(),
                "tag".to_string(),
                "--ignore-tag".to_string(),
                "tag1".to_string(),
            ];
            let mut expected = LogListenerOptions::default();
            expected.local.ignore_tags.insert("tag".to_string());
            expected.local.ignore_tags.insert("tag1".to_string());
            parse_flag_test_helper(&args, Some(&expected));
        }

        #[test]
        fn pid() {
            let args = vec!["--pid".to_string(), "123".to_string()];
            let mut expected = LogListenerOptions::default();
            expected.filter.filter_by_pid = true;
            expected.filter.pid = 123;
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
            let mut expected = LogListenerOptions::default();
            expected.filter.filter_by_tid = true;
            expected.filter.tid = 123;
            parse_flag_test_helper(&args, Some(&expected));
        }

        #[test]
        fn tid_fail() {
            let args = vec!["--tid".to_string(), "123a".to_string()];
            parse_flag_test_helper(&args, None);
        }

        #[test]
        fn severity() {
            let mut expected = LogListenerOptions::default();
            expected.filter.min_severity = LogLevelFilter::None;
            for s in vec!["INFO", "WARN", "ERROR", "FATAL"] {
                let mut args = vec!["--severity".to_string(), s.to_string()];
                expected.filter.min_severity = LogLevelFilter::from_primitive(
                    expected.filter.min_severity.into_primitive() + 1,
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
            let mut expected = LogListenerOptions::default();
            expected.filter.verbosity = 2;
            expected.filter.min_severity = LogLevelFilter::None;
            parse_flag_test_helper(&args, Some(&expected));
        }

        #[test]
        fn severity_verbosity_together() {
            let args = vec![
                "--verbosity".to_string(),
                "2".to_string(),
                "--severity".to_string(),
                "DEBUG".to_string(),
            ];
            parse_flag_test_helper(&args, None);

            let args = vec![
                "--severity".to_string(),
                "DEBUG".to_string(),
                "--verbosity".to_string(),
                "2".to_string(),
            ];
            parse_flag_test_helper(&args, None);
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
        fn file() {
            let mut expected = LogListenerOptions::default();
            expected.local.file = Some("/data/test".to_string());
            let args = vec!["--file".to_string(), "/data/test".to_string()];
            parse_flag_test_helper(&args, Some(&expected));
        }

        #[test]
        fn file_empty() {
            let args = Vec::new();
            let expected = LogListenerOptions::default();
            parse_flag_test_helper(&args, Some(&expected));
        }

        #[test]
        fn clock() {
            let args = vec!["--clock".to_string(), "UTC".to_string()];
            let mut expected = LogListenerOptions::default();
            expected.local.clock = Clock::UTC;
            parse_flag_test_helper(&args, Some(&expected));
        }

        #[test]
        fn clock_fail() {
            let args = vec!["--clock".to_string(), "CLUCK!!".to_string()];
            parse_flag_test_helper(&args, None);
        }

        #[test]
        fn time_format() {
            let args = vec!["--time_format".to_string(), "%H:%M:%S".to_string()];
            let mut expected = LogListenerOptions::default();
            expected.local.time_format = "%H:%M:%S".to_string();
            parse_flag_test_helper(&args, Some(&expected));
        }

        #[test]
        fn tag_edge_case() {
            let mut args = vec!["--tag".to_string()];
            let mut tag = "a".to_string();
            for _ in 1..MAX_TAG_LEN_BYTES {
                tag.push('a');
            }
            args.push(tag.clone());
            let mut expected = LogListenerOptions::default();
            expected.filter.tags.push(tag);
            parse_flag_test_helper(&args, Some(&expected));

            args[1] = "tag1".to_string();
            expected.filter.tags[0] = args[1].clone();
            for i in 1..MAX_TAGS {
                args.push("--tag".to_string());
                args.push(format!("tag{}", i));
                expected.filter.tags.push(format!("tag{}", i));
            }
            parse_flag_test_helper(&args, Some(&expected));
        }

        #[test]
        fn tag_fail() {
            let mut args = vec!["--tag".to_string()];
            let mut tag = "a".to_string();
            for _ in 0..MAX_TAG_LEN_BYTES {
                tag.push('a');
            }
            args.push(tag);
            parse_flag_test_helper(&args, None);

            args[1] = "tag1".to_string();
            for i in 0..MAX_TAGS + 5 {
                args.push("--tag".to_string());
                args.push(format!("tag{}", i));
            }
            parse_flag_test_helper(&args, None);
        }
    }
}
