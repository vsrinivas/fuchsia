// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! library for host-side of filesystem integrity host-target interaction tests.

#![deny(missing_docs)]

use {
    ffx_isolate,
    rand::random,
    std::{
        fmt,
        path::PathBuf,
        process::{ExitStatus, Output},
        sync::{
            atomic::{AtomicU64, Ordering},
            Arc,
        },
        time::Duration,
    },
    thiserror::Error,
};

pub mod steps;
pub use steps::RebootType;
use steps::{LoadStep, RebootStep, SetupStep, TestStep, VerifyStep};

pub mod integration;

const BLACKOUT_DEVICE_LABEL: &'static str = "blackout";

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

/// An error occurred running a command on the target system. Contains the exit status, stdout, and
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

impl From<ffx_isolate::CommandOutput> for CommandError {
    fn from(out: ffx_isolate::CommandOutput) -> Self {
        CommandError(out.status, out.stdout, out.stderr)
    }
}

/// An error occurred while attempting to reboot the system.
#[derive(Debug, Error)]
pub enum RebootError {
    /// The path to the relay device required for hard-rebooting the target doesn't exist.
    #[error("device does not exist: {:?}", _0)]
    MissingDevice(PathBuf),

    /// An io error occurred during rebooting. Maybe we failed to write to the device.
    #[error("io error: {:?}", _0)]
    IoError(#[from] std::io::Error),

    /// The command we executed on the target failed.
    #[error("command error: {:?}", _0)]
    Command(#[from] CommandError),
}

/// Error used for the host-side of the blackout library.
#[derive(Debug, Error)]
pub enum BlackoutError {
    /// Something went wrong!
    #[error("error: {}", _0)]
    AnyhowError(#[from] anyhow::Error),

    /// We got an error when trying to reboot.
    #[error("failed to reboot: {:?}", _0)]
    Reboot(#[from] RebootError),

    /// We failed to run the command on the host. Specifically, when the spawn or something fails,
    /// not when the command itself returns a non-zero exit code.
    #[error("host command failed: {:?}", _0)]
    HostCommand(#[from] std::io::Error),

    /// Timed out during target discovery.
    #[error("no targets found after 5s: {:?}", _0)]
    TargetDiscoveryTimeout(CommandError),

    /// We got an error from the ffx command.
    #[error("failed to run an ffx command: {:?}", _0)]
    FfxError(CommandError),

    /// A failure in the setup step
    #[error("failed to setup test: {:?}", _0)]
    SetupError(CommandError),

    /// Specifically the verification step failed. This indicates an actual test failure as opposed
    /// to a failure of the test framework or environmental failure.
    #[error("verification failed: {:?}", _0)]
    Verification(CommandError),
}

/// Blackout is a power-failure testing framework for the filesystems. This host-side harness runs
/// operations on the configured target device for generating load on the filesystem, then reboots
/// the device after a certain amount of time using a configured reboot mechanism. By default, it
/// runs one iteration of this test. Options are provided for running the test until failure or
/// running the test N times and collecting failure statistics.
#[derive(Clone)]
pub struct CommonOpts {
    /// The optional label for the partition to run the test on. If non is provided, a default will
    /// be used.
    pub device_label: Option<String>,
    /// The optional path to the block device on the target device to use for testing. If none is
    /// provided, the test will find an appropriate device. WARNING: the test can (and likely
    /// will!) format this device. Don't use a main system partition!
    pub device_path: Option<String>,
    /// [Optional] A seed to use for all random operations. Tests are NOT deterministic relative to
    /// the provided seed. The operations will be identical, but because of the non-deterministic
    /// timing-dependent nature of the tests, the exact time the reboot is triggered in relation to
    /// the operations is not guaranteed.
    ///
    /// One will be randomly generated if not provided. When performing the same test multiple times
    /// in one run, a new seed will be generated for each run if one was not provided.
    pub seed: Option<u64>,
    /// Reboot type. There are three options
    /// 1. Soft reboot - we reboot the system using ffx target reboot
    /// 2. Hard reboot with a serial power relay - we reboot the system by writing bytes to a
    /// serial device that we assume is a power relay. Includes a path to the power relay. Probably
    /// the highest-numbered /dev/ttyUSB[N]. If in doubt, try removing it and seeing what
    /// disappears from /dev.
    /// 3. Hard reboot with the infra dmc command - we reboot the system by calling the dmc binary
    /// provided by infra. This command cycles the power for us using some kind of http accessible
    /// power strip, but the details are abstracted behind the set-power-state command.
    pub reboot: RebootType,
    /// Run the test N number of times, collecting statistics on the number of failures.
    pub iterations: Option<u64>,
    /// Run the test until a verification failure is detected, then exit.
    pub run_until_failure: bool,
}

/// the seed for a run of the test.
#[derive(Clone, Debug)]
pub enum Seed {
    /// the seed is constant over multiple runs of the test.
    Constant(u64),
    /// the seed is a random value for every run of the test, generated by `random`.
    Variable(Arc<AtomicU64>),
}

impl Seed {
    fn new(maybe_seed: Option<u64>) -> Seed {
        match maybe_seed {
            Some(seed) => Seed::Constant(seed),
            None => Seed::Variable(Arc::new(AtomicU64::new(random()))),
        }
    }

    fn reroll(&self) {
        match self {
            Seed::Constant(_) => (),
            Seed::Variable(seed) => seed.store(random(), Ordering::Relaxed),
        }
    }

    fn get(&self) -> u64 {
        match self {
            Seed::Constant(seed) => *seed,
            Seed::Variable(seed) => seed.load(Ordering::Relaxed),
        }
    }
}

impl fmt::Display for Seed {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            Seed::Constant(seed) => write!(f, "{}", seed),
            Seed::Variable(seed) => {
                write!(f, "{}", seed.load(Ordering::Relaxed))
            }
        }
    }
}

#[derive(Clone, Debug)]
enum RunMode {
    Once,
    Iterations(u64),
    IterationsUntilFailure(u64),
}

/// Test definition. This contains all the information to make a test reproducible in a particular
/// environment, and allows host binaries to configure the steps taken by the test.
struct Test {
    package: String,
    component: String,
    seed: Seed,
    device_label: String,
    device_path: Option<String>,
    reboot_type: RebootType,
    run_mode: RunMode,
    steps: Vec<Box<dyn TestStep>>,
}

impl fmt::Display for Test {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(
            f,
            "Test {{
    package: {:?},
    component: {:?},
    seed: {:?},
    device_label: {:?},
    device_path: {:?},
    reboot_type: {:?},
    run_mode: {:?},
}}",
            self.package,
            self.component,
            self.seed,
            self.device_label,
            self.device_path,
            self.reboot_type,
            self.run_mode,
        )
    }
}

impl Test {
    /// Create a new test with the provided name. The name needs to match the associated package
    /// that will be present on the target device. The package should be callable with `run` from
    /// the command line.
    pub fn new_component(package: &'static str, component: &'static str, opts: CommonOpts) -> Test {
        Test {
            package: package.to_string(),
            component: component.to_string(),
            seed: Seed::new(opts.seed),
            device_label: opts.device_label.unwrap_or(BLACKOUT_DEVICE_LABEL.to_string()),
            device_path: opts.device_path,
            reboot_type: opts.reboot,
            run_mode: match (opts.iterations, opts.run_until_failure) {
                (None, false) => RunMode::Once,
                (None, true) => panic!("run until failure requires multiple iterations"),
                (Some(iterations), false) => RunMode::Iterations(iterations),
                (Some(iterations), true) => RunMode::IterationsUntilFailure(iterations),
            },
            steps: Vec::new(),
        }
    }

    /// Add a custom test step implementation.
    pub fn add_step(&mut self, step: Box<dyn TestStep>) -> &mut Self {
        self.steps.push(step);
        self
    }

    /// Run the defined test steps. Prints the test definition before execution. Attempts to re-roll
    /// the random seed before the test run.
    async fn run_test(&self) -> Result<(), BlackoutError> {
        self.seed.reroll();

        println!("{}", self);

        for step in &self.steps {
            step.execute().await?;
        }

        Ok(())
    }

    async fn run_iterations(&self, iterations: u64) -> Result<(), BlackoutError> {
        let mut failures = 0u64;
        let mut flukes = 0u64;

        for runs in 1..iterations + 1 {
            println!("{}", box_message(format!("test run #{}", runs)));

            match self.run_test().await {
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

    async fn run_iterations_until_failure(&self, iterations: u64) -> Result<(), BlackoutError> {
        for runs in 1..iterations + 1 {
            println!("{}", box_message(format!("test run #{}", runs)));
            match self.run_test().await {
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
    pub async fn run(self) -> Result<(), BlackoutError> {
        match self.run_mode {
            RunMode::Once => self.run_test().await,
            RunMode::Iterations(iterations) => self.run_iterations(iterations).await,
            RunMode::IterationsUntilFailure(iterations) => {
                self.run_iterations_until_failure(iterations).await
            }
        }
    }
}

/// A test environment. This wraps a test configuration with the environmental context to run it,
/// such as the isolated ffx instance.
pub struct TestEnv {
    test: Test,
    isolated_ffx: Arc<ffx_isolate::Isolate>,
}

impl TestEnv {
    /// Create a new test with the provided name. The name needs to match the associated package
    /// that will be present on the target device. The package should be callable with `ffx
    /// component run` from the command line.
    ///
    /// At this point, the test environment will also perform target discovery, selecting either a
    /// node specified by $FUCHSIA_NODENAME or whatever the first target that gets enumerated by
    /// ffx is.
    ///
    /// If either creating the isolated ffx instance or the target discovery fails, this function
    /// will panic.
    pub async fn new(package: &'static str, component: &'static str, opts: CommonOpts) -> TestEnv {
        let ffx_path = std::env::current_exe().expect("could not determine own path");
        let ffx_path = std::fs::canonicalize(ffx_path).expect("could not canonicalize own path");
        let ssh_key =
            ffx_config::get::<String, _>("ssh.priv").await.expect("could not get ssh key").into();
        let isolate = Arc::new(
            ffx_isolate::Isolate::new("blackout-ffx", ffx_path, ssh_key)
                .await
                .expect("failed to make new isolated ffx"),
        );

        TestEnv { test: Test::new_component(package, component, opts), isolated_ffx: isolate }
    }

    /// Add a test step for setting up the filesystem in the way we want it for the test. This
    /// executes the `setup` subcommand on the target binary and waits for completion, checking the
    /// result.
    pub fn setup_step(&mut self) -> &mut Self {
        self.test.add_step(Box::new(SetupStep::new(
            self.isolated_ffx.clone(),
            &self.test.package,
            &self.test.component,
            self.test.seed.clone(),
            &self.test.device_label,
            self.test.device_path.clone(),
        )));
        self
    }

    /// Add a test step for generating load on the device using the `test` subcommand on the target
    /// binary. This load doesn't terminate. After `duration`, it checks to make sure the command is
    /// still running, then return.
    pub fn load_step(&mut self, duration: Duration) -> &mut Self {
        self.test.add_step(Box::new(LoadStep::new(
            self.isolated_ffx.clone(),
            &self.test.package,
            &self.test.component,
            self.test.seed.clone(),
            &self.test.device_label,
            self.test.device_path.clone(),
            duration,
        )));
        self
    }

    /// Add a reboot step. This reboots the target machine using the configured reboot mechanism.
    pub fn reboot_step(&mut self, bootserver: bool) -> &mut Self {
        self.test.add_step(Box::new(RebootStep::new(
            self.isolated_ffx.clone(),
            &self.test.reboot_type,
            bootserver,
        )));
        self
    }

    /// Add a verify step. This runs the `verify` subcommand on the target binary, waiting for
    /// completion, and checks the result. The verification is done in a retry loop, attempting to
    /// run the verification command `num_retries` times, sleeping for `retry_timeout` duration
    /// between each attempt.
    pub fn verify_step(&mut self, num_retries: u32, retry_timeout: Duration) -> &mut Self {
        self.test.add_step(Box::new(VerifyStep::new(
            self.isolated_ffx.clone(),
            &self.test.package,
            &self.test.component,
            self.test.seed.clone(),
            &self.test.device_label,
            self.test.device_path.clone(),
            num_retries,
            retry_timeout,
        )));
        self
    }

    /// Run the provided test. The test is provided as a function that takes a clone of the command
    /// line options and produces a test to run. This
    /// There are essentially 4 possible types of test runs that we may be doing here.
    /// 1. one iteration (iterations == None && run_until_failure == false)
    ///    this is the simplest case. run one iteration of the test by constructing the test and
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
    pub async fn run(self) -> Result<(), BlackoutError> {
        self.test.run().await
    }
}

#[cfg(test)]
mod tests {
    use super::BlackoutError as Error;
    use super::{BlackoutError, CommandError, CommonOpts, RebootType, Test, TestStep};
    use {
        async_trait::async_trait,
        fuchsia_async as fasync,
        std::{
            os::unix::process::ExitStatusExt,
            process::ExitStatus,
            sync::{
                atomic::{AtomicBool, AtomicU64, Ordering},
                Arc,
            },
        },
    };

    struct FakeStep<F>
    where
        F: Fn(u64) -> Result<(), Error> + Send + Sync,
    {
        res: F,
        runs: Arc<AtomicU64>,
    }
    impl<F> FakeStep<F>
    where
        F: Fn(u64) -> Result<(), Error> + Send + Sync,
    {
        fn new(res: F) -> (FakeStep<F>, Arc<AtomicU64>) {
            let runs = Arc::new(AtomicU64::new(0));
            (FakeStep { res, runs: runs.clone() }, runs)
        }
    }
    #[async_trait]
    impl<F> TestStep for FakeStep<F>
    where
        F: Fn(u64) -> Result<(), Error> + Send + Sync,
    {
        async fn execute(&self) -> Result<(), BlackoutError> {
            self.runs.fetch_add(1, Ordering::Relaxed);
            (self.res)(self.runs.load(Ordering::Relaxed))
        }
    }

    fn fake_test(iterations: Option<u64>, run_until_failure: bool) -> Test {
        let opts = CommonOpts {
            device_label: None,
            device_path: None,
            seed: None,
            reboot: RebootType::Software,
            iterations,
            run_until_failure,
        };
        Test::new_component("fake_package", "fake_component", opts)
    }

    #[fasync::run_singlethreaded(test)]
    async fn run_once_executes_steps_once() {
        // using the run once mode, we execute all our test steps once, in order. run once is how
        // all the other run modes execute their test steps, so if this is solid then we just test
        // iterations and exit modes for the rest.

        let step1_exec1 = Arc::new(AtomicBool::new(false));
        let step1_exec2 = step1_exec1.clone();
        let step1_func = move |_| {
            assert!(!step1_exec1.load(Ordering::Relaxed), "step one already executed");
            step1_exec1.store(true, Ordering::Relaxed);
            Ok(())
        };
        let step2_func = move |_| {
            assert!(step1_exec2.load(Ordering::Relaxed), "step two executed before step one");
            Ok(())
        };
        let (step1, step1_runs) = FakeStep::new(step1_func);
        let (step2, step2_runs) = FakeStep::new(step2_func);

        let mut test = fake_test(None, false);
        test.add_step(Box::new(step1)).add_step(Box::new(step2));

        test.run().await.expect("failed to run test");

        assert_eq!(step1_runs.load(Ordering::Relaxed), 1);
        assert_eq!(step2_runs.load(Ordering::Relaxed), 1);
    }

    #[fasync::run_singlethreaded(test)]
    async fn run_once_exits_on_failure() {
        let (step1, step1_runs) = FakeStep::new(|_| Ok(()));
        let (step2, step2_runs) = FakeStep::new(|_| {
            Err(BlackoutError::Verification(CommandError(
                ExitStatus::from_raw(1),
                "(fake stdout)".into(),
                "(fake stderr)".into(),
            )))
        });
        let (step3, step3_runs) = FakeStep::new(|_| panic!("step 3 should never be run"));
        let mut test = fake_test(None, false);
        test.add_step(Box::new(step1)).add_step(Box::new(step2)).add_step(Box::new(step3));
        match test.run().await {
            Err(BlackoutError::Verification(..)) => (),
            Ok(..) => panic!("test returned Ok on an error"),
            Err(..) => panic!("test returned an unexpected error"),
        }
        assert_eq!(step1_runs.load(Ordering::Relaxed), 1);
        assert_eq!(step2_runs.load(Ordering::Relaxed), 1);
        assert_eq!(step3_runs.load(Ordering::Relaxed), 0);
    }

    #[fasync::run_singlethreaded(test)]
    async fn run_n_executes_steps_n_times() {
        // using the iterations mode, we execute all our test steps `iterations` number of times. we
        // expect that no matter the error value returned from the test, we will execute the
        // expected number of times.
        let iterations = 10;
        let (step, runs) = FakeStep::new(|_| Ok(()));
        let mut test = fake_test(Some(iterations), false);
        test.add_step(Box::new(step));
        test.run().await.expect("failed to run test");
        assert_eq!(
            runs.load(Ordering::Relaxed),
            iterations,
            "step wasn't run the expected number of times"
        );
    }

    #[fasync::run_singlethreaded(test)]
    async fn run_n_executes_steps_n_times_verify_failure() {
        let iterations = 10;
        let (step, runs) = FakeStep::new(|_| {
            Err(BlackoutError::Verification(CommandError(
                ExitStatus::from_raw(1),
                "(fake stdout)".into(),
                "(fake stderr)".into(),
            )))
        });
        let mut test = fake_test(Some(iterations), false);
        test.add_step(Box::new(step));
        test.run().await.expect("failed to run test");
        assert_eq!(
            runs.load(Ordering::Relaxed),
            iterations,
            "step wasn't run the expected number of times"
        );
    }

    #[fasync::run_singlethreaded(test)]
    async fn run_n_executes_steps_n_times_other_error() {
        let iterations = 10;
        let (step, runs) = FakeStep::new(|_| {
            Err(BlackoutError::FfxError(CommandError(
                ExitStatus::from_raw(1),
                "(fake stdout)".into(),
                "(fake stderr)".into(),
            )))
        });
        let mut test = fake_test(Some(iterations), false);
        test.add_step(Box::new(step));
        test.run().await.expect("failed to run test");
        assert_eq!(
            runs.load(Ordering::Relaxed),
            iterations,
            "step wasn't run the expected number of times"
        );
    }
}
