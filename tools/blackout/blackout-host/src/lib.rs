// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! library for host-side of filesystem integrity host-target interaction tests.

#![deny(missing_docs)]

use {
    rand::random,
    std::{
        cell::Cell,
        fmt,
        path::PathBuf,
        process::{ExitStatus, Output},
        rc::Rc,
        time::Duration,
    },
    structopt::StructOpt,
    thiserror::Error,
};

pub mod steps;
use steps::{LoadStep, OperationStep, RebootStep, RebootType, SetupStep, TestStep, VerifyStep};

pub mod integration;

fn box_message(message: String) -> String {
    let line_len = message.len() + 2;
    let line: String = std::iter::repeat('━').take(line_len).collect();
    format!(
        "┏{line}┓
┃ {message} ┃
┗{line}┛",
        message = message,
        line = line
    )
}

/// An error occured running a command on the target system. Contains the exit status, stdout, and
/// stderr of the command.
#[derive(Debug, Error)]
#[error(
    "failed to run command: {}\n\
               stdout:\n\
               {}\n\
               stderr:\n\
               {}",
    _0,
    _1,
    _2
)]
pub struct CommandError(ExitStatus, String, String);

impl From<Output> for CommandError {
    /// Convert the std::process::Output of a command to an error. Mostly takes care of converting
    /// the stdout and stderr into strings from Vec<u8>.
    fn from(out: Output) -> Self {
        let stdout = String::from_utf8(out.stdout).expect("stdout not utf8");
        let stderr = String::from_utf8(out.stderr).expect("stderr not utf8");
        CommandError(out.status, stdout, stderr)
    }
}

/// An error occured while attempting to reboot the system.
#[derive(Debug, Error)]
pub enum RebootError {
    /// The path to the relay device required for hard-rebooting the target doesn't exist.
    #[error("device does not exist: {:?}", _0)]
    MissingDevice(PathBuf),

    /// An io error occured during rebooting. Maybe we failed to write to the device.
    #[error("io error: {:?}", _0)]
    IoError(#[from] std::io::Error),

    /// The command we executed on the target failed.
    #[error("command error: {:?}", _0)]
    Command(#[from] CommandError),
}

/// Error used for the host-side of the blackout library.
#[derive(Debug, Error)]
pub enum BlackoutError {
    /// We got an error when trying to reboot.
    #[error("failed to reboot: {:?}", _0)]
    Reboot(#[from] RebootError),

    /// We failed to run the command on the host. Specifically, when the spawm or something fails,
    /// not when the command itself returns a non-zero exit code.
    #[error("host command failed: {:?}", _0)]
    HostCommand(#[from] std::io::Error),

    /// We got an ssh failure. We know it's an ssh failure because it returned 255.
    #[error("failed to ssh into '{}': {:?}", _0, _1)]
    Ssh(String, CommandError),

    /// The command run on the target device failed to run. Either it returned a non-zero exit code
    /// or it exited when it shouldn't have.
    #[error("target command failed: {}", _0)]
    TargetCommand(CommandError),

    /// Specifically the verification step failed. This indicates an actual test failure as opposed
    /// to a failure of the test framework or environmental failure.
    #[error("verification failed: {}", _0)]
    Verification(CommandError),
}

/// Blackout is a power-failure testing framework for the filesystems. This host-side harness runs
/// operations on the configured target device for generating load on the filesystem, then reboots
/// the device after a certain amount of time using a configured reboot mechanism. By default, it
/// runs one iteration of this test. Options are provided for running the test until failure or
/// running the test N times and collecting failure statistics.
#[derive(Clone, StructOpt)]
#[structopt(rename_all = "kebab-case")]
pub struct CommonOpts {
    /// The block device on the target device to use for testing. WARNING: the test can (and likely
    /// will!) format this device. Don't use a main system partition!
    pub block_device: String,
    /// The target device to ssh into and execute the test on. A good way to configure this locally
    /// is by using `$(fx get-device-addr)`.
    pub target: String,
    /// [Optional] A seed to use for all random operations. Tests are NOT deterministic relative to
    /// the provided seed. The operations will be identical, but because of the non-deterministic
    /// timing-dependent nature of the tests, the exact time the reboot is triggered in relation to
    /// the operations is not guaranteed.
    ///
    /// One will be randomly generated if not provided. When performing the same test multiple times
    /// in one run, a new seed will be generated for each run if one was not provided.
    #[structopt(short, long)]
    pub seed: Option<u64>,
    /// Path to a power relay for cutting the power to a device. Probably the highest-numbered
    /// /dev/ttyUSB[N]. If in doubt, try removing it and seeing what disappears from /dev. When a
    /// relay is provided, the harness automatically switches to use hardware reboots.
    #[structopt(short, long)]
    pub relay: Option<PathBuf>,
    /// Run the test N number of times, collecting statistics on the number of failures.
    #[structopt(short, long)]
    pub iterations: Option<u64>,
    /// Run the test until a verification failure is detected, then exit.
    #[structopt(short = "f", long, requires = "iterations")]
    pub run_until_failure: bool,
    /// Path to the ssh private key to use when authenticating with the target device. If neither
    /// this flag or the --ssh-agent flag is set, the test will open `$FUCHSIA_DIR/.ssh/pkey` if
    /// `$FUCHSIA_DIR` exists, otherwise it will attempt to open `$CWD/.ssh/pkey`. If none of these
    /// attempts succeed the test will fail to run.
    #[structopt(short = "k", long)]
    pub ssh_key: Option<String>,
    /// Use the ssh agent to authenticate with the target device. If both this option and --ssh-key
    /// are provided, --ssk-key will take precedence.
    #[structopt(short = "a", long)]
    pub ssh_agent: bool,
}

/// the seed for a run of the test.
#[derive(Clone, Debug)]
pub enum Seed {
    /// the seed is constant over multiple runs of the test.
    Constant(u64),
    /// the seed is a random value for every run of the test, generated by `random`.
    Variable(Rc<Cell<u64>>),
}

impl Seed {
    fn new(maybe_seed: Option<u64>) -> Seed {
        match maybe_seed {
            Some(seed) => Seed::Constant(seed),
            None => Seed::Variable(Rc::new(Cell::new(random()))),
        }
    }

    fn reroll(&self) {
        match self {
            Seed::Constant(_) => (),
            Seed::Variable(seed_cell) => seed_cell.set(random()),
        }
    }
}

impl fmt::Display for Seed {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            Seed::Constant(seed) => write!(f, "{}", seed),
            Seed::Variable(seed_cell) => write!(f, "{}", seed_cell.get()),
        }
    }
}

#[derive(Clone, Debug)]
enum RunMode {
    Once,
    Iterations(u64),
    IterationsUntilFailure(u64),
}

/// method for authenticating over ssh
#[derive(Clone, Debug)]
enum SshAuth {
    Agent,
    PKey(String),
}

impl SshAuth {
    /// generate the required ssh args for using the auth method
    fn args(&self) -> Vec<String> {
        match &self {
            SshAuth::Agent => vec![],
            SshAuth::PKey(key) => vec!["-o".into(), format!("IdentityFile={}", key)],
        }
    }
}

/// Unconfigured test. Knows how to configure itself based on the set of common options.
pub struct UnconfiguredTest {
    package: &'static str,
}

/// Test definition. This contains all the information to make a test reproducible in a particular
/// environment, and allows host binaries to configure the steps taken by the test.
pub struct Test {
    /// net address of the target device.
    target: String,
    bin: String,
    seed: Seed,
    block_device: String,
    reboot_type: RebootType,
    run_mode: RunMode,
    auth: SshAuth,
    steps: Vec<Box<dyn TestStep>>,
}

impl fmt::Display for Test {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(
            f,
            "Test {{
    target: {:?},
    bin: {:?},
    seed: {:?},
    block_device: {:?},
    reboot_type: {:?},
    run_mode: {:?},
    auth: {:?},
}}",
            self.target,
            self.bin,
            self.seed,
            self.block_device,
            self.reboot_type,
            self.run_mode,
            self.auth,
        )
    }
}

impl UnconfiguredTest {
    /// Collect command line options for configuring the test steps. This uses the [`CommonOpts`]
    /// struct with StructOpt.
    pub fn collect_options(self) -> Test {
        self.add_options(CommonOpts::from_args())
    }

    /// Add the set of common options manually. This can be used if additional options need to be
    /// collected at run time that aren't one of the common options. The CommonOpts struct can be
    /// flattened into the defined CLI using structopt (see their docs for how).
    pub fn add_options(self, opts: CommonOpts) -> Test {
        Test {
            target: opts.target,
            bin: format!("run {}", self.package),
            seed: Seed::new(opts.seed),
            block_device: opts.block_device,
            reboot_type: match opts.relay {
                Some(relay) => RebootType::Hardware(relay),
                None => RebootType::Software,
            },
            run_mode: match (opts.iterations, opts.run_until_failure) {
                (None, false) => RunMode::Once,
                (None, true) => panic!("run until failure requires multiple iterations"),
                (Some(iterations), false) => RunMode::Iterations(iterations),
                (Some(iterations), true) => RunMode::IterationsUntilFailure(iterations),
            },
            auth: match (opts.ssh_key, opts.ssh_agent) {
                (Some(key), _) => SshAuth::PKey(key),
                (None, true) => SshAuth::Agent,
                (None, false) => {
                    if let Ok(fuchsia_dir) = std::env::var("FUCHSIA_DIR") {
                        SshAuth::PKey(format!("{}/.ssh/pkey", fuchsia_dir))
                    } else if let Ok(cwd) = std::env::current_dir() {
                        SshAuth::PKey(format!("{}/.ssh/pkey", cwd.to_str().unwrap()))
                    } else {
                        panic!("can't figure out where to get private key - use --ssh-key");
                    }
                }
            },
            steps: Vec::new(),
        }
    }
}

impl Test {
    /// Create a new test with the provided name. The name needs to match the associated package that
    /// will be present on the target device. The package should be callable with `run` from the
    /// command line.
    pub fn new(package: &'static str) -> UnconfiguredTest {
        UnconfiguredTest { package }
    }

    /// Add a test step for setting up the filesystem in the way we want it for the test. This
    /// executes the `setup` subcommand on the target binary and waits for completion, checking the
    /// result.
    pub fn setup_step(mut self) -> Self {
        self.steps.push(Box::new(SetupStep::new(
            &self.target,
            &self.auth,
            &self.bin,
            self.seed.clone(),
            &self.block_device,
        )));
        self
    }

    /// Add a test step for generating load on the device using the `test` subcommand on the target
    /// binary. This load doesn't terminate. After `duration`, it checks to make sure the command is
    /// still running, then return.
    pub fn load_step(mut self, duration: Duration) -> Self {
        self.steps.push(Box::new(LoadStep::new(
            &self.target,
            &self.auth,
            &self.bin,
            self.seed.clone(),
            &self.block_device,
            duration,
        )));
        self
    }

    /// Add an operation step. This runs the `test` subcommand on the target binary to completion and
    /// checks the result.
    pub fn operation_step(mut self) -> Self {
        self.steps.push(Box::new(OperationStep::new(
            &self.target,
            &self.auth,
            &self.bin,
            self.seed.clone(),
            &self.block_device,
        )));
        self
    }

    /// Add a reboot step. This reboots the target machine using the configured reboot mechanism,
    /// then waits for the machine to come back up. Right now, it waits by sleeping for 30 seconds.
    /// TODO(fxbug.dev/34504): instead of waiting for 30 seconds, we should have a retry loop around the ssh in
    /// the verification step.
    pub fn reboot_step(mut self) -> Self {
        self.steps.push(Box::new(RebootStep::new(&self.target, &self.auth, &self.reboot_type)));
        self
    }

    /// Add a verify step. This runs the `verify` subcommand on the target binary, waiting for
    /// completion, and checks the result. The verification is done in a retry loop, attempting to
    /// run the verification command `num_retries` times, sleeping for `retry_timeout` duration
    /// between each attempt.
    pub fn verify_step(mut self, num_retries: u32, retry_timeout: Duration) -> Self {
        self.steps.push(Box::new(VerifyStep::new(
            &self.target,
            &self.auth,
            &self.bin,
            self.seed.clone(),
            &self.block_device,
            num_retries,
            retry_timeout,
        )));
        self
    }

    /// Add a custom test step implementation.
    pub fn add_step(mut self, step: Box<dyn TestStep>) -> Self {
        self.steps.push(step);
        self
    }

    /// Run the defined test steps. Prints the test definition before execution. Attempts to re-roll
    /// the random seed before the test run.
    fn run_test(&self) -> Result<(), BlackoutError> {
        self.seed.reroll();

        println!("{}", self);

        for step in &self.steps {
            step.execute()?;
        }

        Ok(())
    }

    /// Run the test once. Used either directly, or as the main test driver for the multi-run modes.
    ///
    /// This function is separated from run_test for rather silly technical reasons. In rust, we can
    /// have a main function return a result, which rust automatically prints out on failure, setting
    /// the error code to some non-zero value. However, it prints out the Debug version of the error,
    /// which is rather hard to read, as it's smushed onto one line. Instead of requiring all the
    /// test implementations to figure it out on their own, our implementation pretty-prints out any
    /// errors we find for you! We continue to return the result so that the exit code is properly
    /// set.
    fn run_once(&self) -> Result<(), BlackoutError> {
        match self.run_test() {
            r @ Ok(..) => r,
            Err(e) => {
                println!("{}", e);
                Err(e)
            }
        }
    }

    fn run_iterations(&self, iterations: u64) -> Result<(), BlackoutError> {
        let mut failures = 0u64;
        let mut flukes = 0u64;

        for runs in 1..iterations + 1 {
            println!("{}", box_message(format!("test run #{}", runs)));

            match self.run_once() {
                Ok(()) => (),
                Err(BlackoutError::Verification(_)) => failures += 1,
                Err(_) => flukes += 1,
            }

            println!("runs:                    {}", runs);
            println!("failures:                {}", failures);
            println!("non-verification errors: {}", flukes);
            println!(
                "failure percentage:      {:.2}%",
                (failures as f64 / (runs - flukes) as f64) * 100.0
            );
        }

        Ok(())
    }

    fn run_iterations_until_failure(&self, iterations: u64) -> Result<(), BlackoutError> {
        for runs in 1..iterations + 1 {
            println!("{}", box_message(format!("test run #{}", runs)));
            match self.run_once() {
                Ok(()) => (),
                Err(e @ BlackoutError::Verification(_)) => return Err(e),
                Err(_) => (),
            }
        }
        Ok(())
    }

    /// Run the provided test. The test is provided as a function that takes a clone of the command
    /// line options and produces a test to run. This
    /// There are essentially 4 possible types of test runs that we may be doing here.
    /// 1. one iteration (iterations == None && run_until_failure == false)
    ///    this is the simplist case. run one iteration of the test by constructing the test and
    ///    calling test.run(), returning the result.
    /// 2. some number of iterations (iterations == Some(N) && run_until_failure == false)
    ///    essentially the InfiniteExecutor code, except instead of an infinite loop we use a
    ///    bounded one.
    /// 3. run until verification failure (iterations == None && run_until_failure == true)
    ///    run tests until an Error::Verification is returned. keep track of the number of runs,
    ///    but there is no need to tabulate other errors.
    /// 4. run until verification failure, except with a max number of iterations
    ///    (iterations == Some(N) && run_until_failure == true)
    ///    if both flags are present, we combine the functionality. only run a certain number of
    ///    iterations, but quit early if there is a failure instead of aggregating the results.
    pub fn run(self) -> Result<(), BlackoutError> {
        match self.run_mode {
            RunMode::Once => self.run_once(),
            RunMode::Iterations(iterations) => self.run_iterations(iterations),
            RunMode::IterationsUntilFailure(iterations) => {
                self.run_iterations_until_failure(iterations)
            }
        }
    }
}

#[cfg(test)]
mod tests {
    use super::BlackoutError as Error;
    use super::{BlackoutError, CommandError, CommonOpts, Test, TestStep};
    use std::{cell::Cell, os::unix::process::ExitStatusExt, process::ExitStatus, rc::Rc};

    struct FakeStep<F>
    where
        F: Fn(u64) -> Result<(), Error>,
    {
        res: F,
        runs: Rc<Cell<u64>>,
    }
    impl<F> FakeStep<F>
    where
        F: Fn(u64) -> Result<(), Error>,
    {
        fn new(res: F) -> (FakeStep<F>, Rc<Cell<u64>>) {
            let runs = Rc::new(Cell::new(0));
            (FakeStep { res, runs: runs.clone() }, runs)
        }
    }
    impl<F> TestStep for FakeStep<F>
    where
        F: Fn(u64) -> Result<(), Error>,
    {
        fn execute(&self) -> Result<(), BlackoutError> {
            self.runs.set(self.runs.get() + 1);
            (self.res)(self.runs.get())
        }
    }

    fn fake_test(iterations: Option<u64>, run_until_failure: bool) -> Test {
        let test = Test::new("fake package");
        let opts = CommonOpts {
            block_device: "/fake/block/device".into(),
            target: "fake target".into(),
            seed: None,
            relay: None,
            iterations,
            run_until_failure,
            ssh_key: Some("/fake/ssh/key".into()),
            ssh_agent: false,
        };
        test.add_options(opts)
    }

    #[test]
    fn run_once_executes_steps_once() {
        // using the run once mode, we execute all our test steps once, in order. run once is how
        // all the other run modes execute their test steps, so if this is solid then we just test
        // iterations and exit modes for the rest.

        let step1_exec1 = Rc::new(Cell::new(false));
        let step1_exec2 = step1_exec1.clone();
        let step1_func = move |_| {
            assert!(!step1_exec1.get(), "step one already executed");
            step1_exec1.set(true);
            Ok(())
        };
        let step2_func = move |_| {
            assert!(step1_exec2.get(), "step two executed before step one");
            Ok(())
        };
        let (step1, step1_runs) = FakeStep::new(step1_func);
        let (step2, step2_runs) = FakeStep::new(step2_func);

        let test = fake_test(None, false).add_step(Box::new(step1)).add_step(Box::new(step2));

        test.run().expect("failed to run test");

        assert_eq!(step1_runs.get(), 1);
        assert_eq!(step2_runs.get(), 1);
    }

    #[test]
    fn run_once_exits_on_failure() {
        let (step1, step1_runs) = FakeStep::new(|_| Ok(()));
        let (step2, step2_runs) = FakeStep::new(|_| {
            Err(BlackoutError::Verification(CommandError(
                ExitStatus::from_raw(1),
                "(fake stdout)".into(),
                "(fake stderr)".into(),
            )))
        });
        let (step3, step3_runs) = FakeStep::new(|_| panic!("step 3 should never be run"));
        let test = fake_test(None, false)
            .add_step(Box::new(step1))
            .add_step(Box::new(step2))
            .add_step(Box::new(step3));
        match test.run() {
            Err(BlackoutError::Verification(..)) => (),
            Ok(..) => panic!("test returned Ok on an error"),
            Err(..) => panic!("test returned an unexpected error"),
        }
        assert_eq!(step1_runs.get(), 1);
        assert_eq!(step2_runs.get(), 1);
        assert_eq!(step3_runs.get(), 0);
    }

    #[test]
    fn run_n_executes_steps_n_times() {
        // using the iterations mode, we execute all our test steps `iterations` number of times. we
        // expect that no matter the error value returned from the test, we will execute the
        // expected number of times.
        let iterations = 10;
        let (step, runs) = FakeStep::new(|_| Ok(()));
        let test = fake_test(Some(iterations), false).add_step(Box::new(step));
        test.run().expect("failed to run test");
        assert_eq!(runs.get(), iterations, "step wasn't run the expected number of times");
    }

    #[test]
    fn run_n_executes_steps_n_times_verify_failure() {
        let iterations = 10;
        let (step, runs) = FakeStep::new(|_| {
            Err(BlackoutError::Verification(CommandError(
                ExitStatus::from_raw(1),
                "(fake stdout)".into(),
                "(fake stderr)".into(),
            )))
        });
        let test = fake_test(Some(iterations), false).add_step(Box::new(step));
        test.run().expect("failed to run test");
        assert_eq!(runs.get(), iterations, "step wasn't run the expected number of times");
    }

    #[test]
    fn run_n_executes_steps_n_times_other_error() {
        let iterations = 10;
        let (step, runs) = FakeStep::new(|_| {
            Err(BlackoutError::Ssh(
                "(fake target)".into(),
                CommandError(
                    ExitStatus::from_raw(1),
                    "(fake stdout)".into(),
                    "(fake stderr)".into(),
                ),
            ))
        });
        let test = fake_test(Some(iterations), false).add_step(Box::new(step));
        test.run().expect("failed to run test");
        assert_eq!(runs.get(), iterations, "step wasn't run the expected number of times");
    }
}
