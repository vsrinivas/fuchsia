// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![deny(warnings)]

use std::env;

// Include the generated FIDL bindings for the `Logger` service.
extern crate garnet_public_lib_logger_fidl;
use garnet_public_lib_logger_fidl::LogFilterOptions;

const FX_LOG_MAX_TAGS: usize = 5;
const FX_LOG_MAX_TAG_LEN: usize = 63;

fn default_log_filter_options() -> LogFilterOptions {
    LogFilterOptions {
        filter_by_pid: false,
        pid: 0,
        filter_by_severity: false,
        min_severity: 0,
        filter_by_tid: false,
        tid: 0,
        tags: vec![],
    }
}

fn help(name: &str) -> String {
    format!(
        "Usage: {} [flags]
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
                if tag.len() > FX_LOG_MAX_TAG_LEN {
                    return Err(format!(
                        "'{}' should not be more then {} characters",
                        tag, FX_LOG_MAX_TAG_LEN
                    ));
                }
                options.tags.push(String::from(tag.as_ref()));
                if options.tags.len() > FX_LOG_MAX_TAGS {
                    return Err(format!("Max tags allowed: {}", FX_LOG_MAX_TAGS));
                }
            }
            "--severity" => {
                options.filter_by_severity = true;
                match args[i + 1].as_ref() {
                    "INFO" => {
                        options.min_severity = 0;
                    }
                    "WARN" => {
                        options.min_severity = 1;
                    }
                    "ERROR" => {
                        options.min_severity = 2;
                    }
                    "FATAL" => {
                        options.min_severity = 3;
                    }
                    a => {
                        return Err(format!("Invalid severity: {}", a));
                    }
                }
            }
            "--verbosity" => {
                options.filter_by_severity = true;
                match args[i + 1].parse::<i32>() {
                    Ok(v) => {
                        if v <= 0 {
                            return Err(format!(
                                "Invalid verbosity: '{}', should be positive integer greater than 0.",
                                args[i+1]
                            ));
                        }
                        options.min_severity = -v;
                    }
                    Err(_) => {
                        return Err(format!(
                            "Invalid verbosity: '{}', should be positive integer greater than 0.",
                            args[i + 1]
                        ));
                    }
                }
            }
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

fn main() {
    let args: Vec<String> = env::args().collect();
    if args.len() > 1 && (args[1] == "--help" || args[1] == "-h") {
        println!("{}\n", help(args[0].as_ref()));
        return;
    }
    let _options = match parse_flags(&args[1..]) {
        Err(e) => {
            eprintln!("{}\n{}\n", e, help(args[0].as_ref()));
            return;
        }
        Ok(o) => o,
    };
}

#[cfg(test)]
mod tests {
    use super::*;

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
            expected.filter_by_severity = true;
            expected.min_severity = -1;
            for s in vec!["INFO", "WARN", "ERROR", "FATAL"] {
                let mut args = vec!["--severity".to_string(), s.to_string()];
                expected.min_severity = expected.min_severity + 1;
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
            expected.filter_by_severity = true;
            expected.min_severity = -2;
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
            for _ in 1..FX_LOG_MAX_TAG_LEN {
                tag.push('a');
            }
            args.push(tag.clone());
            let mut expected = default_log_filter_options();
            expected.tags.push(tag);
            parse_flag_test_helper(&args, Some(&expected));

            args[1] = "tag1".to_string();
            expected.tags[0] = args[1].clone();
            for i in 1..FX_LOG_MAX_TAGS {
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
            for _ in 0..FX_LOG_MAX_TAG_LEN {
                tag.push('a');
            }
            args.push(tag);
            parse_flag_test_helper(&args, None);

            args[1] = "tag1".to_string();
            for i in 0..FX_LOG_MAX_TAGS {
                args.push("--tag".to_string());
                args.push(format!("tag{}", i));
            }
            parse_flag_test_helper(&args, None);
        }
    }
}
