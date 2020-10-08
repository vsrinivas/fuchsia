// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    argh::{EarlyExit, FromArgs},
    fidl_fuchsia_test_manager::HarnessMarker,
};

#[derive(FromArgs, Default, PartialEq, Eq, Debug)]
// TODO(61417): use argh for test_args when the feature is implemented and rolled.
/// Arguments. Use option delimiter(--) to pass arguments to the test suite.
struct Args {
    /// test timeout. Exits with -`ZX_ERR_TIMED_OUT` if the test times out.
    #[argh(option, short = 't')]
    timeout: Option<u32>,

    /// test url. Test should implement `fuchsia.test.Suite` protocol.
    #[argh(positional)]
    test_url: String,

    /// test filter. A glob pattern for matching tests.
    #[argh(option)]
    test_filter: Option<String>,

    /// whether to also run tests that have been marked disabled/ignored by the test author.
    #[argh(switch)]
    also_run_disabled_tests: bool,

    /// run test cases in parallel, up to the number provided.
    #[argh(option)]
    parallel: Option<u16>,
}

// parses args, returns `Args` and everything after '--'.
fn parse_args(cmd: Vec<String>) -> Result<(Args, Option<Vec<String>>), EarlyExit> {
    let mut splits = cmd.splitn(2, |s| s == "--");
    let s: Vec<&str> = splits.next().unwrap().iter().map(|s| s.as_str()).collect();
    let args = Args::from_args(&[s[0]], &s[1..])?;
    let rest = splits.next().map(|v| v.to_vec());
    Ok((args, rest))
}

#[fuchsia_async::run_singlethreaded]
async fn main() {
    let (args, test_args) = parse_args(std::env::args().collect()).unwrap_or_else(|early_exit| {
        println!("{}", early_exit.output);
        std::process::exit(match early_exit.status {
            Ok(()) => 0,
            Err(()) => 1,
        })
    });

    let Args { timeout, test_url, test_filter, also_run_disabled_tests, parallel } = args;

    let harness = fuchsia_component::client::connect_to_service::<HarnessMarker>()
        .expect("connecting to HarnessProxy");

    match run_test_suite_lib::run_tests_and_get_outcome(run_test_suite_lib::TestParams {
        test_url,
        timeout: timeout.and_then(std::num::NonZeroU32::new),
        test_filter,
        also_run_disabled_tests,
        parallel,
        test_args: test_args,
        harness,
    })
    .await
    {
        run_test_suite_lib::Outcome::Passed => {}
        run_test_suite_lib::Outcome::Timedout => {
            std::process::exit(-fuchsia_zircon::Status::TIMED_OUT.into_raw());
        }
        run_test_suite_lib::Outcome::Failed
        | run_test_suite_lib::Outcome::Inconclusive
        | run_test_suite_lib::Outcome::Error => {
            std::process::exit(1);
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn args_vec(mut args: Vec<&str>) -> Vec<String> {
        let mut a = vec!["test_program"];
        a.append(&mut args);
        a.iter().map(|s| s.to_string()).collect()
    }

    #[test]
    // As we have custom parsing when user passes "--", making sure everything works fine.
    fn test_parse_args() {
        let url = "foo.cm";
        let mut expected_args = Args { test_url: url.to_string(), ..Default::default() };
        expected_args.test_url = url.to_string();

        let (args, test_args) = parse_args(args_vec(vec![url])).unwrap();
        assert_eq!(args, expected_args);
        assert_eq!(test_args, None);

        let (args, test_args) = parse_args(args_vec(vec![url, "--"])).unwrap();
        assert_eq!(args, expected_args);
        assert_eq!(test_args, Some(vec![]));

        // make sure we can parse --help flag when user passes "--"
        let err = parse_args(args_vec(vec![url, "--help", "--"])).unwrap_err();
        assert_eq!(err.status, Ok(()));

        // make sure we can parse --help flag without "--"
        let err = parse_args(args_vec(vec![url, "--help"])).unwrap_err();
        assert_eq!(err.status, Ok(()));

        // make sure we can catch arg errors when user passes "--"
        let err = parse_args(args_vec(vec![url, "--timeout", "a", "--"])).unwrap_err();
        assert_eq!(err.status, Err(()));

        // make sure we can catch arg errors without "--"
        let err = parse_args(args_vec(vec![url, "--timeout", "a"])).unwrap_err();
        assert_eq!(err.status, Err(()));

        // make sure we can parse args when user passes "--"
        let (args, test_args) = parse_args(args_vec(vec![url, "--timeout", "2", "--"])).unwrap();
        expected_args.timeout = Some(2);
        assert_eq!(args, expected_args);
        assert_eq!(test_args, Some(vec![]));

        // make sure we can parse args without "--"
        let (args, test_args) = parse_args(args_vec(vec![url, "--timeout", "2"])).unwrap();
        assert_eq!(args, expected_args);
        assert_eq!(test_args, None);

        // make sure we can parse args after "--"
        let (args, test_args) = parse_args(args_vec(vec![
            url,
            "--timeout",
            "2",
            "--",
            "--arg1",
            "some_random_str",
            "-arg2",
        ]))
        .unwrap();
        assert_eq!(args, expected_args);
        assert_eq!(
            test_args,
            Some(vec!["--arg1".to_owned(), "some_random_str".to_owned(), "-arg2".to_owned()])
        );

        // parse_args works with multiple "--"
        let (args, test_args) = parse_args(args_vec(vec![
            url,
            "--timeout",
            "2",
            "--",
            "--",
            "--arg1",
            "some_random_str",
            "--",
            "-arg2",
        ]))
        .unwrap();
        assert_eq!(args, expected_args);
        assert_eq!(
            test_args,
            Some(vec![
                "--".to_owned(),
                "--arg1".to_owned(),
                "some_random_str".to_owned(),
                "--".to_owned(),
                "-arg2".to_owned()
            ])
        );
    }
}
