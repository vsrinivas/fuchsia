// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! library for host-side of filesystem integrity host-target interaction tests.

#![deny(missing_docs)]

use {
    failure::{self, Fail},
    rand::random,
    std::{
        fmt,
        path::PathBuf,
        process::{ExitStatus, Output},
        time::Duration,
    },
    structopt::StructOpt,
};

pub mod steps;
use steps::{LoadStep, OperationStep, RebootStep, RebootType, SetupStep, TestStep, VerifyStep};

/// An error occured running a command on the target system. Contains the exit status, stdout, and
/// stderr of the command.
#[derive(Debug, Fail)]
#[fail(
    display = "failed to run command: {}\n\
               stdout:\n\
               {}\n\
               stderr:\n\
               {}",
    _0, _1, _2
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
#[derive(Debug, Fail)]
pub enum RebootError {
    /// The path to the relay device required for hard-rebooting the target doesn't exist.
    #[fail(display = "device does not exist: {:?}", _0)]
    MissingDevice(PathBuf),

    /// An io error occured during rebooting. Maybe we failed to write to the device.
    #[fail(display = "io error: {:?}", _0)]
    IoError(#[cause] std::io::Error),

    /// The command we executed on the target failed.
    #[fail(display = "command error: {:?}", _0)]
    Command(#[cause] CommandError),
}

impl From<std::io::Error> for RebootError {
    fn from(error: std::io::Error) -> Self {
        RebootError::IoError(error)
    }
}

impl From<CommandError> for RebootError {
    fn from(error: CommandError) -> Self {
        RebootError::Command(error)
    }
}

/// Error used for the host-side of the blackout library.
#[derive(Debug, Fail)]
pub enum Error {
    /// We got an error when trying to reboot.
    #[fail(display = "failed to reboot: {:?}", _0)]
    Reboot(#[cause] RebootError),

    /// We failed to run the command on the host. Specifically, when the spawm or something fails,
    /// not when the command itself returns a non-zero exit code.
    #[fail(display = "host command failed: {:?}", _0)]
    HostCommand(#[cause] std::io::Error),

    /// We got an ssh failure. We know it's an ssh failure because it returned 255.
    #[fail(display = "failed to ssh into '{}': {:?}", _0, _1)]
    Ssh(String, #[cause] CommandError),

    /// The command run on the target device failed to run. Either it returned a non-zero exit code
    /// or it exited when it shouldn't have.
    #[fail(display = "target command failed: {}", _0)]
    TargetCommand(#[cause] CommandError),

    /// Specifically the verification step failed. This indicates an actual test failure as opposed
    /// to a failure of the test framework or environmental failure.
    #[fail(display = "verification failed: {}", _0)]
    Verification(#[cause] CommandError),
}

impl From<RebootError> for Error {
    fn from(error: RebootError) -> Self {
        Error::Reboot(error)
    }
}

impl From<std::io::Error> for Error {
    fn from(error: std::io::Error) -> Self {
        Error::HostCommand(error)
    }
}

/// Common options for the host-side test runners
#[derive(StructOpt)]
#[structopt(rename_all = "kebab-case")]
pub struct CommonOpts {
    /// The block device on the target device to use for testing. WARNING: the test can (and likely
    /// will!) format this device. Don't use a main system partition!
    pub block_device: String,
    /// The target device to ssh into and execute the test on. A good way to configure this locally
    /// is by prefixing the binary with `FUCHSIA_IPV4_ADDR=$(fx netaddr --fuchsia)`
    #[structopt(env = "FUCHSIA_IPV4_ADDR")]
    pub target: String,
    /// [Optional] A seed to use for all random operations. Tests are deterministic relative to the
    /// provided seed. One will be randomly generated if not provided.
    #[structopt(short, long)]
    pub seed: Option<u32>,
    /// Path to a power relay for cutting the power to a device. Probably the highest-numbered
    /// /dev/ttyUSB[N]. If in doubt, try removing it and seeing what disappears from /dev.
    #[structopt(short, long)]
    pub relay: Option<PathBuf>,
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
    seed: u32,
    block_device: String,
    reboot_type: RebootType,
    steps: Vec<Box<dyn TestStep>>,
}

impl fmt::Display for Test {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        write!(
            f,
            "Test {{
    target: {:?},
    bin: {:?},
    seed: {:?},
    block_device: {:?},
    reboot_type: {:?},
}}",
            self.target, self.bin, self.seed, self.block_device, self.reboot_type
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
            seed: opts.seed.unwrap_or_else(random),
            block_device: opts.block_device,
            reboot_type: match opts.relay {
                Some(relay) => RebootType::Hardware(relay),
                None => RebootType::Software,
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
            &self.bin,
            self.seed,
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
            &self.bin,
            self.seed,
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
            &self.bin,
            self.seed,
            &self.block_device,
        )));
        self
    }

    /// Add a reboot step. This reboots the target machine using the configured reboot mechanism,
    /// then waits for the machine to come back up. Right now, it waits by sleeping for 30 seconds.
    /// TODO(34504): instead of waiting for 30 seconds, we should have a retry loop around the ssh in
    /// the verification step.
    pub fn reboot_step(mut self) -> Self {
        self.steps.push(Box::new(RebootStep::new(&self.target, &self.reboot_type)));
        self
    }

    /// Add a verify step. This runs the `verify` subcommand on the target binary, waiting for
    /// completion, and checks the result. The verification is done in a retry loop, attempting to
    /// run the verification command `num_retries` times, sleeping for `retry_timeout` duration
    /// between each attempt.
    pub fn verify_step(mut self, num_retries: u32, retry_timeout: Duration) -> Self {
        self.steps.push(Box::new(VerifyStep::new(
            &self.target,
            &self.bin,
            self.seed,
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

    /// Run the defined test steps. Prints the test definition before execution.
    pub fn run(self) -> Result<(), Error> {
        println!("{}", self);

        for step in self.steps {
            step.execute()?;
        }

        Ok(())
    }
}
