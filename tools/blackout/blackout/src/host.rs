// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! library for host-side of filesystem integrity host-target interaction tests.

#![deny(missing_docs)]

use {
    failure::{bail, Error, ResultExt},
    rand::random,
    std::{
        fs::OpenOptions,
        io::Write,
        path::{Path, PathBuf},
        process::{Child, Command, Output, Stdio},
        thread::sleep,
        time::Duration,
    },
    structopt::StructOpt,
};

static SSH_OPTIONS: &'static [&str] = &["-o", "ConnectTimeout=100"];

/// Common options for the host-side test runners
#[derive(StructOpt)]
#[structopt(rename_all = "kebab-case")]
pub struct CommonOpts {
    /// The block device on the target device to use for testing. WARNING: the test can (and likely
    /// will!) format this device. Don't use a main system partition!
    pub block_device: String,
    /// The target device to ssh into and execute the test on.
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

/// Test definition. This contains all the information to make a test reproducible in a particular
/// environment.
#[derive(Debug)]
pub struct Test {
    target: String,
    bin: &'static str,
    seed: u32,
    block_device: String,
    relay: Option<PathBuf>,
}

// TODO(sdemos): the final implementation will also have to handle a CI environment where hard
// rebooting is done by calling a script that will be in our environment.
fn hard_reboot(dev: impl AsRef<Path>) -> Result<(), Error> {
    if !dev.as_ref().exists() {
        bail!("provided device does not exist");
    }
    let mut relay = OpenOptions::new().read(false).write(true).create(false).open(dev)?;
    relay.write_all(&[0x01])?;
    sleep(Duration::from_millis(10));
    relay.write_all(&[0x02])?;
    Ok(())
}

impl Test {
    /// Create a new test by specifying the command to run on the target system.
    pub fn new(bin: &'static str, opts: CommonOpts) -> Result<Self, Error> {
        let seed = opts.seed.unwrap_or_else(random);

        Ok(Test {
            target: opts.target,
            bin,
            seed,
            block_device: opts.block_device,
            relay: opts.relay,
        })
    }

    fn ssh(&self) -> Command {
        let mut command = Command::new("fx");
        command.arg("ssh").args(SSH_OPTIONS).arg(&self.target);
        command
    }

    fn run_bin(&self) -> Command {
        let mut command = self.ssh();
        command.arg(self.bin).arg(self.seed.to_string()).arg(&self.block_device);
        command
    }

    /// Run a subcommand of the originally provided binary on the target. The command is spawned as a
    /// separate process, and a reference to the child process is returned. stdout and stderr are
    /// piped (see [`std::process::Stdio::piped()`] for details).
    pub fn run_spawn(&self, subc: &str) -> Result<Child, Error> {
        let child = self
            .run_bin()
            .arg(subc)
            .stdout(Stdio::piped())
            .stderr(Stdio::piped())
            .spawn()
            .context("failed to spawn command")?;

        Ok(child)
    }

    /// Run a subcommand to completion and collect the output from the process.
    pub fn run_output(&self, subc: &str) -> Result<Output, Error> {
        let out = self.run_bin().arg(subc).output().context("failed to run command")?;

        Ok(out)
    }

    /// Reboot the target system using `dm reboot`.
    pub fn soft_reboot(&self) -> Result<(), Error> {
        let _ = self.ssh().arg("dm").arg("reboot").status().context("failed to reboot")?;

        Ok(())
    }

    /// Reboot the target system by cutting the power to it with the provided relay.
    pub fn hard_reboot(&self) -> Result<(), Error> {
        match &self.relay {
            None => bail!("no relay device provided"),
            Some(r) => hard_reboot(r),
        }
    }
}
