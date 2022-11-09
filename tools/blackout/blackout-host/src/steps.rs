// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! test steps

use {
    crate::{BlackoutError, CommandError, RebootError, Seed},
    anyhow::{bail, ensure, Context},
    async_trait::async_trait,
    ffx_isolate,
    serde_json::Value,
    std::{
        fs::OpenOptions,
        io::Write,
        path::{Path, PathBuf},
        sync::Arc,
        thread::sleep,
        time::Duration,
    },
};

/// Type of reboot to perform.
#[derive(Clone, Debug)]
pub enum RebootType {
    /// Perform a software reboot using dm reboot on the target device over ssh.
    Software,
    /// Perform a hardware reboot by writing a byte to a relay device. THIS OPTION IS LIKELY TO
    /// CHANGE IN THE FUTURE.
    Hardware(PathBuf),
    /// Perform a hardware reboot by calling the infra device management tool.
    Dmc,
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

async fn get_target(ffx: &ffx_isolate::Isolate) -> Result<String, anyhow::Error> {
    if let Ok(name) = std::env::var("FUCHSIA_NODENAME") {
        return Ok(name);
    }

    // ensure a daemon is spun up first, so we have a moment to discover targets.
    let start = std::time::Instant::now();
    loop {
        let out = ffx.ffx(&["ffx", "target", "list"]).await?;
        if out.stdout.len() > 10 {
            break;
        }
        if start.elapsed() > std::time::Duration::from_secs(5) {
            bail!("No targets found after 5s")
        }
    }

    let out = ffx.ffx(&["target", "list", "-f", "j"]).await.context("getting target list")?;

    ensure!(out.status.success(), "Looking up a target name failed: {:?}", out);

    let targets: Value =
        serde_json::from_str(&out.stdout).context("parsing output from target list")?;

    let targets =
        targets.as_array().ok_or(anyhow::anyhow!("expected target list ot return an array"))?;

    let target = targets
        .iter()
        .find(|target| {
            target["nodename"] != ""
                && target["target_state"]
                    .as_str()
                    .map(|s| s.to_lowercase().contains("product"))
                    .unwrap_or(false)
        })
        .ok_or(anyhow::anyhow!("did not find any named targets in a product state"))?;
    target["nodename"]
        .as_str()
        .map(|s| s.to_string())
        .ok_or(anyhow::anyhow!("expected product state target to have a nodename"))
}

#[async_trait]
trait Runner: Send + Sync {
    /// Call the Setup method on the client.
    async fn setup(&self) -> Result<(), BlackoutError>;
    /// Call the Load method on the client.
    async fn test(&self, duration: Option<Duration>) -> Result<(), BlackoutError>;
    /// Call the verify method on the client.
    async fn verify(&self) -> Result<(), BlackoutError>;
}

struct FfxRunner {
    ffx: Arc<ffx_isolate::Isolate>,
    package: String,
    component: String,
    seed: Seed,
    device_label: String,
    device_path: Option<String>,
}

impl FfxRunner {
    fn new(
        ffx: Arc<ffx_isolate::Isolate>,
        package: &str,
        component: &str,
        seed: Seed,
        device_label: &str,
        device_path: Option<String>,
    ) -> Arc<dyn Runner> {
        Arc::new(FfxRunner {
            ffx,
            package: package.to_string(),
            component: component.to_string(),
            seed,
            device_label: device_label.to_string(),
            device_path,
        })
    }

    async fn start_component(&self, target: &str) -> Result<(), BlackoutError> {
        let run_output = self
            .ffx
            .ffx(&[
                "--target",
                target,
                "component",
                "run",
                "--recreate",
                "/core/ffx-laboratory:blackout-target",
                &format!("fuchsia-pkg://fuchsia.com/{}#meta/{}.cm", self.package, self.component),
            ])
            .await?;
        if run_output.status.success() {
            Ok(())
        } else {
            Err(BlackoutError::FfxError(run_output.into()))
        }
    }

    async fn run_subcommand(
        &self,
        target: &str,
        command: &str,
        duration: Option<Duration>,
    ) -> Result<(), CommandError> {
        let seed = self.seed.get().to_string();
        let duration = duration.map(|d| d.as_secs().to_string());
        let mut args = vec![
            "--target",
            target,
            "--config",
            "storage_dev=true",
            "storage",
            "blackout",
            "step",
            command,
            &self.device_label,
            &seed,
        ];
        if let Some(path) = &self.device_path {
            args.push("--device-path");
            args.push(path);
        }
        if let Some(duration) = &duration {
            args.push("--duration");
            args.push(duration);
        }
        let output = self.ffx.ffx(&args).await.expect("failed to convert output");
        if output.status.success() {
            Ok(())
        } else {
            Err(output.into())
        }
    }
}

#[async_trait]
impl Runner for FfxRunner {
    async fn setup(&self) -> Result<(), BlackoutError> {
        let target = get_target(&self.ffx).await?;
        self.start_component(&target).await?;
        self.run_subcommand(&target, "setup", None).await.map_err(|e| BlackoutError::SetupError(e))
    }

    async fn test(&self, duration: Option<Duration>) -> Result<(), BlackoutError> {
        let target = get_target(&self.ffx).await?;
        self.start_component(&target).await?;
        self.run_subcommand(&target, "test", duration).await.map_err(|e| BlackoutError::FfxError(e))
    }

    async fn verify(&self) -> Result<(), BlackoutError> {
        let target = get_target(&self.ffx).await?;
        self.start_component(&target).await?;
        self.run_subcommand(&target, "verify", None)
            .await
            .map_err(|e| BlackoutError::Verification(e))
    }
}

/// A step for a test to take. These steps can be added to the test runner in the root of the host
/// library.
#[async_trait]
pub trait TestStep {
    /// Execute this test step.
    async fn execute(&self) -> Result<(), BlackoutError>;
}

/// A test step for setting up the filesystem in the way we want it for the test. This executes the
/// `setup` subcommand on the target binary and waits for completion, checking the result.
pub struct SetupStep {
    runner: Arc<dyn Runner>,
}

impl SetupStep {
    /// Create a new operation step.
    pub(crate) fn new(
        ffx: Arc<ffx_isolate::Isolate>,
        package: &str,
        component: &str,
        seed: Seed,
        device_label: &str,
        device_path: Option<String>,
    ) -> Self {
        Self { runner: FfxRunner::new(ffx, package, component, seed, device_label, device_path) }
    }
}

#[async_trait]
impl TestStep for SetupStep {
    async fn execute(&self) -> Result<(), BlackoutError> {
        println!("setting up test...");
        self.runner.setup().await
    }
}

/// A test step for generating load on a filesystem. This executes the `test` subcommand on the
/// target binary and then checks to make sure it didn't exit after `duration`.
pub struct LoadStep {
    runner: Arc<dyn Runner>,
    duration: Option<Duration>,
}

impl LoadStep {
    /// Create a new test step.
    pub(crate) fn new(
        ffx: Arc<ffx_isolate::Isolate>,
        package: &str,
        component: &str,
        seed: Seed,
        device_label: &str,
        device_path: Option<String>,
        duration: Option<Duration>,
    ) -> Self {
        Self {
            runner: FfxRunner::new(ffx, package, component, seed, device_label, device_path),
            duration,
        }
    }
}

#[async_trait]
impl TestStep for LoadStep {
    async fn execute(&self) -> Result<(), BlackoutError> {
        println!("generating filesystem load...");
        self.runner.test(self.duration).await?;
        Ok(())
    }
}

/// A test step for rebooting the target machine. This uses the configured reboot mechanism.
pub struct RebootStep {
    ffx: Arc<ffx_isolate::Isolate>,
    reboot_type: RebootType,
    bootserver: bool,
}

impl RebootStep {
    /// Create a new reboot step.
    pub(crate) fn new(
        ffx: Arc<ffx_isolate::Isolate>,
        reboot_type: &RebootType,
        bootserver: bool,
    ) -> Self {
        Self { ffx, reboot_type: reboot_type.clone(), bootserver }
    }
}

#[async_trait]
impl TestStep for RebootStep {
    async fn execute(&self) -> Result<(), BlackoutError> {
        println!("rebooting device...");
        match &self.reboot_type {
            RebootType::Software => {
                let target = get_target(&self.ffx).await?;
                let _ = self.ffx.ffx(&["--target", &target, "target", "reboot"]).await?;
            }
            RebootType::Hardware(relay) => hard_reboot(&relay)?,
            RebootType::Dmc => {
                let target = get_target(&self.ffx).await?;
                let dmc = std::env::var("DMC_PATH").unwrap();
                let output = std::process::Command::new(dmc)
                    .arg("set-power-state")
                    .arg("-server-port")
                    .arg("8000")
                    .arg("-nodename")
                    .arg(target)
                    .arg("-state")
                    .arg("cycle")
                    .output()?;
                if !output.status.success() {
                    return Err(BlackoutError::Reboot(RebootError::Command(output.into())));
                }
            }
        }
        if self.bootserver {
            println!("launching infra bootserver...");
            let _ = crate::integration::run_bootserver()?;
        }
        Ok(())
    }
}

/// A test step for verifying the machine. This executes the `verify` subcommand on the target binary
/// and waits for completion, checking the result.
pub struct VerifyStep {
    runner: Arc<dyn Runner>,
    num_retries: u32,
    retry_timeout: Duration,
}

impl VerifyStep {
    /// Create a new verify step. Verification is done in a retry loop, attempting to run the
    /// verification command `num_retries` times and sleeping for `retry_timeout` duration in between
    /// each attempt.
    pub(crate) fn new(
        ffx: Arc<ffx_isolate::Isolate>,
        package: &str,
        component: &str,
        seed: Seed,
        device_label: &str,
        device_path: Option<String>,
        num_retries: u32,
        retry_timeout: Duration,
    ) -> Self {
        Self {
            runner: FfxRunner::new(ffx, package, component, seed, device_label, device_path),
            num_retries,
            retry_timeout,
        }
    }
}

#[async_trait]
impl TestStep for VerifyStep {
    async fn execute(&self) -> Result<(), BlackoutError> {
        let mut last_error = Ok(());
        let start_time = std::time::Instant::now();
        for i in 1..self.num_retries + 1 {
            println!("verifying device...(attempt #{})", i);
            match self.runner.verify().await {
                Ok(()) => {
                    println!("verification successful.");
                    return Ok(());
                }
                Err(e @ BlackoutError::Verification(..)) => return Err(e),
                Err(e) => {
                    last_error = Err(e);
                    fuchsia_async::Timer::new(self.retry_timeout).await;
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
    use super::{Runner, SetupStep, TestStep, VerifyStep};
    use crate::{BlackoutError, CommandError};
    use {
        async_trait::async_trait,
        fuchsia_async as fasync,
        std::{
            os::unix::process::ExitStatusExt,
            process::ExitStatus,
            sync::{Arc, Mutex},
            time::Duration,
        },
    };

    #[derive(Debug, PartialEq, Clone, Copy)]
    enum ExpectedCommand {
        Setup,
        Test,
        Verify,
    }
    struct FakeRunner<F>
    where
        F: Fn() -> Result<(), BlackoutError> + Send + Sync,
    {
        command: ExpectedCommand,
        res: F,
    }
    impl<F> FakeRunner<F>
    where
        F: Fn() -> Result<(), BlackoutError> + Send + Sync,
    {
        pub fn new(command: ExpectedCommand, res: F) -> FakeRunner<F> {
            FakeRunner { command, res }
        }
    }
    #[async_trait]
    impl<F> Runner for FakeRunner<F>
    where
        F: Fn() -> Result<(), BlackoutError> + Send + Sync,
    {
        async fn setup(&self) -> Result<(), BlackoutError> {
            assert_eq!(self.command, ExpectedCommand::Setup);
            (self.res)()
        }
        async fn test(&self, _duration: Option<Duration>) -> Result<(), BlackoutError> {
            assert_eq!(self.command, ExpectedCommand::Test);
            (self.res)()
        }
        async fn verify(&self) -> Result<(), BlackoutError> {
            assert_eq!(self.command, ExpectedCommand::Verify);
            (self.res)()
        }
    }

    #[fasync::run_singlethreaded(test)]
    async fn setup_success() {
        let step =
            SetupStep { runner: Arc::new(FakeRunner::new(ExpectedCommand::Setup, || Ok(()))) };
        match step.execute().await {
            Ok(()) => (),
            _ => panic!("setup step returned an error on a successful run"),
        }
    }

    #[fasync::run_singlethreaded(test)]
    async fn setup_error() {
        let error = || {
            Err(BlackoutError::SetupError(CommandError(
                ExitStatus::from_raw(1),
                "(fake stdout)".into(),
                "(fake stderr)".into(),
            )))
        };
        let step = SetupStep { runner: Arc::new(FakeRunner::new(ExpectedCommand::Setup, error)) };
        match step.execute().await {
            Err(BlackoutError::SetupError(_)) => (),
            Ok(()) => panic!("setup step returned success when runner failed"),
            _ => panic!("setup step returned an unexpected error"),
        }
    }

    #[fasync::run_singlethreaded(test)]
    async fn verify_success() {
        let step = VerifyStep {
            runner: Arc::new(FakeRunner::new(ExpectedCommand::Verify, || Ok(()))),
            num_retries: 10,
            retry_timeout: Duration::from_secs(0),
        };
        match step.execute().await {
            Ok(()) => (),
            _ => panic!("verify step returned an error on a successful run"),
        }
    }

    #[fasync::run_singlethreaded(test)]
    async fn verify_error() {
        let error = || {
            Err(BlackoutError::Verification(CommandError(
                ExitStatus::from_raw(1),
                "(fake stdout)".into(),
                "(fake stderr)".into(),
            )))
        };
        let step = VerifyStep {
            runner: Arc::new(FakeRunner::new(ExpectedCommand::Verify, error)),
            num_retries: 10,
            retry_timeout: Duration::from_secs(0),
        };
        match step.execute().await {
            Err(BlackoutError::Verification(_)) => (),
            Ok(()) => panic!("verify step returned success when runner failed"),
            _ => panic!("verify step returned an unexpected error"),
        }
    }

    #[fasync::run_singlethreaded(test)]
    async fn verify_ssh_error_retry_loop_timeout() {
        let outer_attempts = Arc::new(Mutex::new(0));
        let attempts = outer_attempts.clone();
        let error = move || {
            let mut attempts = attempts.lock().unwrap();
            *attempts += 1;
            Err(BlackoutError::FfxError(CommandError(
                ExitStatus::from_raw(255),
                "(fake stdout)".into(),
                "(fake stderr)".into(),
            )))
        };
        let step = VerifyStep {
            runner: Arc::new(FakeRunner::new(ExpectedCommand::Verify, error)),
            num_retries: 10,
            retry_timeout: Duration::from_secs(0),
        };
        match step.execute().await {
            Err(BlackoutError::FfxError(..)) => (),
            Ok(()) => panic!("verify step returned success when runner failed"),
            _ => panic!("verify step returned an unexpected error"),
        }
        let outer_attempts = outer_attempts.lock().unwrap();
        assert_eq!(*outer_attempts, 10);
    }

    #[fasync::run_singlethreaded(test)]
    async fn verify_ssh_error_retry_loop_success() {
        let outer_attempts = Arc::new(Mutex::new(0));
        let attempts = outer_attempts.clone();
        let error = move || {
            let mut attempts = attempts.lock().unwrap();
            *attempts += 1;
            if *attempts <= 5 {
                Err(BlackoutError::FfxError(CommandError(
                    ExitStatus::from_raw(255),
                    "(fake stdout)".into(),
                    "(fake stderr)".into(),
                )))
            } else {
                Ok(())
            }
        };
        let step = VerifyStep {
            runner: Arc::new(FakeRunner::new(ExpectedCommand::Verify, error)),
            num_retries: 10,
            retry_timeout: Duration::from_secs(0),
        };
        match step.execute().await {
            Ok(()) => (),
            Err(BlackoutError::FfxError(..)) => {
                panic!("verify step returned error when runner succeeded")
            }
            _ => panic!("verify step returned an unexpected error"),
        }
        let outer_attempts = outer_attempts.lock().unwrap();
        assert_eq!(*outer_attempts, 6);
    }
}
