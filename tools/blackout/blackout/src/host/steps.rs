// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! test steps

use {
    crate::{Error, RebootError},
    std::{
        fs::OpenOptions,
        io::Write,
        path::{Path, PathBuf},
        process::{Child, Command, Output, Stdio},
        thread::sleep,
        time::Duration,
    },
};

static SSH_OPTIONS: &'static [&str] = &["-o", "ConnectTimeout=100"];

fn ssh(target: &str) -> Command {
    let mut command = Command::new("fx");
    command.arg("ssh").args(SSH_OPTIONS).arg(target);
    command
}

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

/// Reboot the target system using `dm reboot`.
fn soft_reboot(target: &str) -> Result<(), RebootError> {
    // ignore the return value because it's garbage
    let _ = ssh(target).arg("dm").arg("reboot").status()?;
    Ok(())
}

/// run a target binary on a target device over ssh.
struct Runner {
    target: String,
    bin: String,
    seed: u32,
    block_device: String,
}

impl Runner {
    fn new(target: &str, bin: &str, seed: u32, block_device: &str) -> Self {
        Runner { target: target.into(), bin: bin.into(), seed, block_device: block_device.into() }
    }

    fn run_bin(&self) -> Command {
        let mut command = ssh(&self.target);
        command.arg(&self.bin).arg(self.seed.to_string()).arg(&self.block_device);
        command
    }

    /// Run a subcommand of the originally provided binary on the target. The command is spawned as a
    /// separate process, and a reference to the child process is returned. stdout and stderr are
    /// piped (see [`std::process::Stdio::piped()`] for details).
    pub fn run_spawn(&self, subc: &str) -> Result<Child, Error> {
        let child =
            self.run_bin().arg(subc).stdout(Stdio::piped()).stderr(Stdio::piped()).spawn()?;

        Ok(child)
    }

    /// Run a subcommand to completion and collect the output from the process.
    pub fn run_output(&self, subc: &str) -> Result<Output, Error> {
        let out = self.run_bin().arg(subc).output()?;
        if out.status.success() {
            Ok(out)
        } else if out.status.code().unwrap() == 255 {
            Err(Error::Ssh(self.target.clone(), out.into()))
        } else {
            Err(Error::TargetCommand(out.into()))
        }
    }

    pub fn run(&self, subc: &str) -> Result<(), Error> {
        self.run_output(subc).map(|_| ())
    }
}

/// A step for a test to take. These steps can be added to the test runner in the root of the host
/// library.
pub trait TestStep {
    /// Execute this test step.
    fn execute(&self) -> Result<(), Error>;
}

/// A test step for setting up the filesystem in the way we want it for the test. This executes the
/// `setup` subcommand on the target binary and waits for completion, checking the result.
pub struct SetupStep {
    runner: Runner,
}

impl SetupStep {
    /// Create a new operation step.
    pub fn new(target: &str, bin: &str, seed: u32, block_device: &str) -> Self {
        Self { runner: Runner::new(target, bin, seed, block_device) }
    }
}

impl TestStep for SetupStep {
    fn execute(&self) -> Result<(), Error> {
        println!("setting up test...");
        self.runner.run("setup")
    }
}

/// A test step for generating load on a filesystem. This executes the `test` subcommand on the
/// target binary and then checks to make sure it didn't exit after `duration`.
pub struct LoadStep {
    runner: Runner,
    duration: Duration,
}

impl LoadStep {
    /// Create a new test step.
    pub fn new(target: &str, bin: &str, seed: u32, block_device: &str, duration: Duration) -> Self {
        Self { runner: Runner::new(target, bin, seed, block_device), duration }
    }
}

impl TestStep for LoadStep {
    fn execute(&self) -> Result<(), Error> {
        println!("generating filesystem load...");
        let mut child = self.runner.run_spawn("test")?;

        sleep(self.duration);

        // make sure child process is still running
        if let Some(_) = child.try_wait()? {
            let out = child.wait_with_output()?;
            return Err(Error::TargetCommand(out.into()));
        }

        Ok(())
    }
}

/// A test step for running an operation to completion. This executes the `test` subcommand and waits
/// for completion, checking the result.
pub struct OperationStep {
    runner: Runner,
}

impl OperationStep {
    /// Create a new operation step.
    pub fn new(target: &str, bin: &str, seed: u32, block_device: &str) -> Self {
        Self { runner: Runner::new(target, bin, seed, block_device) }
    }
}

impl TestStep for OperationStep {
    fn execute(&self) -> Result<(), Error> {
        println!("running filesystem operation...");
        self.runner.run("test")
    }
}

/// A test step for rebooting the target machine. This uses the configured reboot mechanism. It waits
/// for 30 seconds before returning to make sure the target machine is back up before returning.
///
/// TODO(34504): instead of waiting 30 seconds, we should have a retry loop on the ssh attempt.
pub struct RebootStep {
    target: String,
    reboot_type: RebootType,
}

impl RebootStep {
    /// Create a new reboot step.
    pub fn new(target: &str, reboot_type: &RebootType) -> Self {
        Self { target: target.into(), reboot_type: reboot_type.clone() }
    }
}

impl TestStep for RebootStep {
    fn execute(&self) -> Result<(), Error> {
        println!("rebooting device...");
        match &self.reboot_type {
            RebootType::Software => soft_reboot(&self.target)?,
            RebootType::Hardware(relay) => hard_reboot(&relay)?,
        }
        Ok(())
    }
}

/// A test step for verifying the machine. This executes the `verify` subcommand on the target binary
/// and waits for completion, checking the result.
pub struct VerifyStep {
    runner: Runner,
    num_retries: u32,
    retry_timeout: Duration,
}

impl VerifyStep {
    /// Create a new verify step. Verification is done in a retry loop, attempting to run the
    /// verification command `num_retries` times and sleeping for `retry_timeout` duration in between
    /// each attempt.
    pub fn new(
        target: &str,
        bin: &str,
        seed: u32,
        block_device: &str,
        num_retries: u32,
        retry_timeout: Duration,
    ) -> Self {
        Self { runner: Runner::new(target, bin, seed, block_device), num_retries, retry_timeout }
    }
}

impl TestStep for VerifyStep {
    fn execute(&self) -> Result<(), Error> {
        let mut last_ssh_error = Ok(());
        for i in 1..self.num_retries + 1 {
            println!("verifying device...(attempt #{})", i);
            match self.runner.run("verify") {
                Ok(()) => {
                    println!("verification successful.");
                    return Ok(());
                }
                Err(ssh_error @ Error::Ssh(..)) => {
                    // always print out the ssh error so it doesn't get buried to help with debugging.
                    println!("{}", ssh_error);
                    last_ssh_error = Err(ssh_error);
                    sleep(self.retry_timeout);
                }
                // during the verification stage, we expect that any time the target command fails,
                // it's a verification failure.
                Err(Error::TargetCommand(e)) => return Err(Error::Verification(e)),
                Err(e) => return Err(e),
            }
        }
        // we failed to ssh into the device too many times in a row. something's wrong.
        last_ssh_error
    }
}
