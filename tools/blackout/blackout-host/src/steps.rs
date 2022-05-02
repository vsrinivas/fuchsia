// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! test steps

use {
    crate::{BlackoutError, RebootError, Seed},
    ffx_isolate,
    std::{
        fs::OpenOptions,
        io::Write,
        path::{Path, PathBuf},
        process::{Child, Command, Output, Stdio},
        sync::Arc,
        thread::sleep,
        time::Duration,
    },
};

const VERIFICATION_FAILURE_EXIT_CODE: i32 = 42;

/// Type of reboot to perform.
#[derive(Clone, Debug)]
pub enum RebootType {
    /// Perform a software reboot using dm reboot on the target device over ssh.
    Software,
    /// Perform a hardware reboot by writing a byte to a relay device. THIS OPTION IS LIKELY TO
    /// CHANGE IN THE FUTURE.
    Hardware(PathBuf),
}

// TODO(sdemos): the final implementation will also have to handle a CI environment where hard
// rebooting is done by calling a script that will be in our environment.
fn hard_reboot(dev: impl AsRef<Path>) -> Result<(), RebootError> {
    if !dev.as_ref().exists() {
        return Err(RebootError::MissingDevice(dev.as_ref().to_path_buf()));
    }
    let mut relay = OpenOptions::new().read(false).write(true).create(false).open(dev)?;
    relay.write_all(&[0x01])?;
    sleep(Duration::from_millis(100));
    relay.write_all(&[0x02])?;
    Ok(())
}

trait Runner {
    /// Run a subcommand of the originally provided binary on the target. The command is spawned as
    /// a separate process, and a reference to the child process is returned. stdout and stderr are
    /// piped (see [`std::process::Stdio::piped()`] for details).
    fn run_spawn(&self, subc: &str) -> Result<Child, BlackoutError>;

    /// Run a subcommand to completion and collect the output from the process.
    fn run_output(&self, subc: &str) -> Result<Output, BlackoutError>;

    /// Run a subcommand to completion but throw out the output.
    fn run(&self, subc: &str) -> Result<(), BlackoutError>;
}

struct FfxRunner {
    ffx: Arc<ffx_isolate::Isolate>,
    target: String,
    bin: String,
    seed: Seed,
    block_device: String,
}

impl FfxRunner {
    fn new(
        ffx: Arc<ffx_isolate::Isolate>,
        target: &str,
        bin: &str,
        seed: Seed,
        block_device: &str,
    ) -> Box<dyn Runner> {
        Box::new(FfxRunner {
            ffx,
            target: target.to_string(),
            bin: bin.to_string(),
            seed,
            block_device: block_device.to_string(),
        })
    }

    fn run_bin(&self) -> Command {
        let mut command = self.ffx.ffx_cmd(&[]);
        command
            .arg("--target")
            .arg(&self.target)
            .arg("component")
            .arg("run-legacy")
            .arg(&self.bin)
            .arg(self.seed.to_string())
            .arg(&self.block_device);
        command
    }
}

impl Runner for FfxRunner {
    fn run_spawn(&self, subc: &str) -> Result<Child, BlackoutError> {
        let child =
            self.run_bin().arg(subc).stdout(Stdio::piped()).stderr(Stdio::piped()).spawn()?;

        Ok(child)
    }
    fn run_output(&self, subc: &str) -> Result<Output, BlackoutError> {
        let out = self.run_bin().arg(subc).output()?;
        if out.status.success() {
            Ok(out)
        } else if out.status.code().unwrap() == VERIFICATION_FAILURE_EXIT_CODE {
            Err(BlackoutError::Verification(out.into()))
        } else {
            Err(BlackoutError::TargetCommand(out.into()))
        }
    }
    fn run(&self, subc: &str) -> Result<(), BlackoutError> {
        self.run_output(subc).map(|_| ())
    }
}

/// A step for a test to take. These steps can be added to the test runner in the root of the host
/// library.
pub trait TestStep {
    /// Execute this test step.
    fn execute(&self) -> Result<(), BlackoutError>;
}

/// A test step for setting up the filesystem in the way we want it for the test. This executes the
/// `setup` subcommand on the target binary and waits for completion, checking the result.
pub struct SetupStep {
    runner: Box<dyn Runner>,
}

impl SetupStep {
    /// Create a new operation step.
    pub(crate) fn new(
        ffx: Arc<ffx_isolate::Isolate>,
        target: &str,
        bin: &str,
        seed: Seed,
        block_device: &str,
    ) -> Self {
        Self { runner: FfxRunner::new(ffx, target, bin, seed, block_device) }
    }
}

impl TestStep for SetupStep {
    fn execute(&self) -> Result<(), BlackoutError> {
        println!("setting up test...");
        self.runner.run("setup")
    }
}

/// A test step for generating load on a filesystem. This executes the `test` subcommand on the
/// target binary and then checks to make sure it didn't exit after `duration`.
pub struct LoadStep {
    runner: Box<dyn Runner>,
    duration: Duration,
}

impl LoadStep {
    /// Create a new test step.
    pub(crate) fn new(
        ffx: Arc<ffx_isolate::Isolate>,
        target: &str,
        bin: &str,
        seed: Seed,
        block_device: &str,
        duration: Duration,
    ) -> Self {
        Self { runner: FfxRunner::new(ffx, target, bin, seed, block_device), duration }
    }
}

impl TestStep for LoadStep {
    fn execute(&self) -> Result<(), BlackoutError> {
        println!("generating filesystem load...");
        let mut child = self.runner.run_spawn("test")?;

        sleep(self.duration);

        // make sure child process is still running
        if let Some(_) = child.try_wait()? {
            let out = child.wait_with_output()?;
            return Err(BlackoutError::TargetCommand(out.into()));
        }

        Ok(())
    }
}

/// A test step for running an operation to completion. This executes the `test` subcommand and waits
/// for completion, checking the result.
pub struct OperationStep {
    runner: Box<dyn Runner>,
}

impl OperationStep {
    /// Create a new operation step.
    pub(crate) fn new(
        ffx: Arc<ffx_isolate::Isolate>,
        target: &str,
        bin: &str,
        seed: Seed,
        block_device: &str,
    ) -> Self {
        Self { runner: FfxRunner::new(ffx, target, bin, seed, block_device) }
    }
}

impl TestStep for OperationStep {
    fn execute(&self) -> Result<(), BlackoutError> {
        println!("running filesystem operation...");
        self.runner.run("test")
    }
}

/// A test step for rebooting the target machine. This uses the configured reboot mechanism.
pub struct RebootStep {
    ffx: Arc<ffx_isolate::Isolate>,
    target: String,
    reboot_type: RebootType,
}

impl RebootStep {
    /// Create a new reboot step.
    pub(crate) fn new(
        ffx: Arc<ffx_isolate::Isolate>,
        target: &str,
        reboot_type: &RebootType,
    ) -> Self {
        Self { ffx, target: target.to_string(), reboot_type: reboot_type.clone() }
    }
}

impl TestStep for RebootStep {
    fn execute(&self) -> Result<(), BlackoutError> {
        println!("rebooting device...");
        match &self.reboot_type {
            RebootType::Software => {
                let _ =
                    self.ffx.ffx_cmd(&["--target", &self.target, "target", "reboot"]).status()?;
            }
            RebootType::Hardware(relay) => hard_reboot(&relay)?,
        }
        Ok(())
    }
}

/// A test step for verifying the machine. This executes the `verify` subcommand on the target binary
/// and waits for completion, checking the result.
pub struct VerifyStep {
    runner: Box<dyn Runner>,
    num_retries: u32,
    retry_timeout: Duration,
}

impl VerifyStep {
    /// Create a new verify step. Verification is done in a retry loop, attempting to run the
    /// verification command `num_retries` times and sleeping for `retry_timeout` duration in between
    /// each attempt.
    pub(crate) fn new(
        ffx: Arc<ffx_isolate::Isolate>,
        target: &str,
        bin: &str,
        seed: Seed,
        block_device: &str,
        num_retries: u32,
        retry_timeout: Duration,
    ) -> Self {
        Self {
            runner: FfxRunner::new(ffx, target, bin, seed, block_device),
            num_retries,
            retry_timeout,
        }
    }
}

impl TestStep for VerifyStep {
    fn execute(&self) -> Result<(), BlackoutError> {
        let mut last_error = Ok(());
        let start_time = std::time::Instant::now();
        for i in 1..self.num_retries + 1 {
            println!("verifying device...(attempt #{})", i);
            match self.runner.run("verify") {
                Ok(()) => {
                    println!("verification successful.");
                    return Ok(());
                }
                Err(e @ BlackoutError::Verification(..)) => return Err(e),
                Err(e) => {
                    // always print out the error so it doesn't get buried to help with debugging.
                    println!("{}", e);
                    last_error = Err(e);
                    sleep(self.retry_timeout);
                }
            }
        }
        let elapsed = std::time::Instant::now().duration_since(start_time);
        println!("stopping verification attempt after {}s", elapsed.as_secs());
        // we failed to run a command on the device too many times in a row. something's wrong.
        last_error
    }
}

#[cfg(test)]
mod tests {
    use super::{OperationStep, Runner, SetupStep, TestStep, VerifyStep};
    use crate::{BlackoutError, CommandError};
    use std::{
        cell::Cell,
        os::unix::process::ExitStatusExt,
        process::{Child, ExitStatus, Output},
        rc::Rc,
        time::Duration,
    };

    struct FakeRunner<F>
    where
        F: Fn() -> Result<(), BlackoutError>,
    {
        command: &'static str,
        res: F,
    }
    impl<F> FakeRunner<F>
    where
        F: Fn() -> Result<(), BlackoutError>,
    {
        pub fn new(command: &'static str, res: F) -> FakeRunner<F> {
            FakeRunner { command, res }
        }
    }
    impl<F> Runner for FakeRunner<F>
    where
        F: Fn() -> Result<(), BlackoutError>,
    {
        fn run_spawn(&self, _subc: &str) -> Result<Child, BlackoutError> {
            unimplemented!()
        }
        fn run_output(&self, _subc: &str) -> Result<Output, BlackoutError> {
            unimplemented!()
        }
        fn run(&self, subc: &str) -> Result<(), BlackoutError> {
            assert_eq!(subc, self.command);
            (self.res)()
        }
    }

    #[test]
    fn setup_success() {
        let step = SetupStep { runner: Box::new(FakeRunner::new("setup", || Ok(()))) };
        match step.execute() {
            Ok(()) => (),
            _ => panic!("setup step returned an error on a successful run"),
        }
    }

    #[test]
    fn setup_error() {
        let error = || {
            Err(BlackoutError::TargetCommand(CommandError(
                ExitStatus::from_raw(1),
                "(fake stdout)".into(),
                "(fake stderr)".into(),
            )))
        };
        let step = SetupStep { runner: Box::new(FakeRunner::new("setup", error)) };
        match step.execute() {
            Err(BlackoutError::TargetCommand(_)) => (),
            Ok(()) => panic!("setup step returned success when runner failed"),
            _ => panic!("setup step returned an unexpected error"),
        }
    }

    #[test]
    fn operation_success() {
        let step = OperationStep { runner: Box::new(FakeRunner::new("test", || Ok(()))) };
        match step.execute() {
            Ok(()) => (),
            _ => panic!("operation step returned an error on a successful run"),
        }
    }

    #[test]
    fn operation_error() {
        let error = || {
            Err(BlackoutError::TargetCommand(CommandError(
                ExitStatus::from_raw(1),
                "(fake stdout)".into(),
                "(fake stderr)".into(),
            )))
        };
        let step = OperationStep { runner: Box::new(FakeRunner::new("test", error)) };
        match step.execute() {
            Err(BlackoutError::TargetCommand(_)) => (),
            Ok(()) => panic!("operation step returned success when runner failed"),
            _ => panic!("operation step returned an unexpected error"),
        }
    }

    #[test]
    fn verify_success() {
        let step = VerifyStep {
            runner: Box::new(FakeRunner::new("verify", || Ok(()))),
            num_retries: 10,
            retry_timeout: Duration::from_secs(0),
        };
        match step.execute() {
            Ok(()) => (),
            _ => panic!("verify step returned an error on a successful run"),
        }
    }

    #[test]
    fn verify_error() {
        let error = || {
            Err(BlackoutError::Verification(CommandError(
                ExitStatus::from_raw(1),
                "(fake stdout)".into(),
                "(fake stderr)".into(),
            )))
        };
        let step = VerifyStep {
            runner: Box::new(FakeRunner::new("verify", error)),
            num_retries: 10,
            retry_timeout: Duration::from_secs(0),
        };
        match step.execute() {
            Err(BlackoutError::Verification(_)) => (),
            Ok(()) => panic!("verify step returned success when runner failed"),
            _ => panic!("verify step returned an unexpected error"),
        }
    }

    #[test]
    fn verify_ssh_error_retry_loop_timeout() {
        let outer_attempts = Rc::new(Cell::new(0));
        let attempts = outer_attempts.clone();
        let error = move || {
            attempts.set(attempts.get() + 1);
            Err(BlackoutError::TargetCommand(CommandError(
                ExitStatus::from_raw(255),
                "(fake stdout)".into(),
                "(fake stderr)".into(),
            )))
        };
        let step = VerifyStep {
            runner: Box::new(FakeRunner::new("verify", error)),
            num_retries: 10,
            retry_timeout: Duration::from_secs(0),
        };
        match step.execute() {
            Err(BlackoutError::TargetCommand(..)) => (),
            Ok(()) => panic!("verify step returned success when runner failed"),
            _ => panic!("verify step returned an unexpected error"),
        }
        assert_eq!(outer_attempts.get(), 10);
    }

    #[test]
    fn verify_ssh_error_retry_loop_success() {
        let outer_attempts = Rc::new(Cell::new(0));
        let attempts = outer_attempts.clone();
        let error = move || {
            attempts.set(attempts.get() + 1);
            if attempts.get() <= 5 {
                Err(BlackoutError::TargetCommand(CommandError(
                    ExitStatus::from_raw(255),
                    "(fake stdout)".into(),
                    "(fake stderr)".into(),
                )))
            } else {
                Ok(())
            }
        };
        let step = VerifyStep {
            runner: Box::new(FakeRunner::new("verify", error)),
            num_retries: 10,
            retry_timeout: Duration::from_secs(0),
        };
        match step.execute() {
            Ok(()) => (),
            Err(BlackoutError::TargetCommand(..)) => {
                panic!("verify step returned error when runner succeeded")
            }
            _ => panic!("verify step returned an unexpected error"),
        }
        assert_eq!(outer_attempts.get(), 6);
    }
}
