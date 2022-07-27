// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! The qemu_base module encapsulates traits and functions specific
//! for engines using QEMU as the emulator platform.

use crate::{arg_templates::process_flag_template, serialization::SerializingEngine};
use anyhow::{anyhow, bail, Context, Result};
use async_trait::async_trait;
use errors::ffx_bail;
use ffx_config::SshKeyFiles;
use ffx_emulator_common::{
    config,
    config::FfxConfigWrapper,
    dump_log_to_out, process, tap_available,
    target::{add_target, is_active, remove_target},
};
use ffx_emulator_config::{
    AccelerationMode, ConsoleType, DeviceConfig, EmulatorConfiguration, EmulatorEngine,
    GuestConfig, NetworkingMode,
};
use fidl_fuchsia_developer_ffx as ffx;
use shared_child::SharedChild;
use std::{
    env, fs,
    fs::File,
    io::{stderr, Write},
    ops::Sub,
    path::PathBuf,
    process::Command,
    str,
    sync::Arc,
    time::Duration,
};

pub(crate) mod femu;
pub(crate) mod qemu;

/// QemuBasedEngine collects the interface for
/// emulator engine implementations that use
/// QEMU as the emulator.
/// This allows the implementation to be shared
/// across multiple engine types.
#[async_trait]
pub(crate) trait QemuBasedEngine: EmulatorEngine + SerializingEngine {
    /// Checks that the required files are present
    fn check_required_files(&self, guest: &GuestConfig) -> Result<()> {
        let kernel_path = &guest.kernel_image;
        let zbi_path = &guest.zbi_image;
        let fvm_path = &guest.fvm_image;

        if !kernel_path.exists() {
            bail!("kernel file {:?} does not exist.", kernel_path);
        }
        if !zbi_path.exists() {
            bail!("zbi file {:?} does not exist.", zbi_path);
        }
        if let Some(file_path) = &fvm_path {
            if !file_path.exists() {
                bail!("fvm file {:?} does not exist.", file_path);
            }
        }
        Ok(())
    }

    /// Stages the source image files in an instance specific directory.
    /// Also resizes the fvms to the desired size and adds the authorized
    /// keys to the zbi.
    /// Returns an updated GuestConfig instance with the file paths set to
    /// the instance paths.
    async fn stage_image_files(
        instance_name: &str,
        guest_config: &GuestConfig,
        device_config: &DeviceConfig,
        reuse: bool,
        config: &FfxConfigWrapper,
    ) -> Result<GuestConfig> {
        let mut updated_guest = guest_config.clone();

        // Create the data directory if needed.
        let mut instance_root: PathBuf = config.file(config::EMU_INSTANCE_ROOT_DIR).await?;
        instance_root.push(instance_name);
        fs::create_dir_all(&instance_root)?;

        let kernel_name = guest_config.kernel_image.file_name().ok_or_else(|| {
            anyhow!("cannot read kernel file name '{:?}'", guest_config.kernel_image)
        });
        let kernel_path = instance_root.join(kernel_name?);
        if kernel_path.exists() && reuse {
            tracing::debug!("Using existing file for {:?}", kernel_path.file_name().unwrap());
        } else {
            fs::copy(&guest_config.kernel_image, &kernel_path)
                .context("cannot stage kernel file")?;
        }

        let zbi_path = instance_root.join(
            guest_config
                .zbi_image
                .file_name()
                .ok_or_else(|| anyhow!("cannot read zbi file name"))?,
        );

        if zbi_path.exists() && reuse {
            tracing::debug!("Using existing file for {:?}", zbi_path.file_name().unwrap());
        } else {
            // Add the authorized public keys to the zbi image to enable SSH access to
            // the guest.
            Self::embed_authorized_keys(&guest_config.zbi_image, &zbi_path, config)
                .await
                .context("cannot embed authorized keys")?;
        }

        let fvm_path = match &guest_config.fvm_image {
            Some(src_fvm) => {
                let fvm_path = instance_root
                    .join(src_fvm.file_name().ok_or_else(|| anyhow!("cannot read fvm file name"))?);
                if fvm_path.exists() && reuse {
                    tracing::debug!("Using existing file for {:?}", fvm_path.file_name().unwrap());
                } else {
                    fs::copy(src_fvm, &fvm_path).context("cannot stage fvm file")?;

                    // Resize the fvm image if needed.
                    let image_size = format!(
                        "{}{}",
                        device_config.storage.quantity,
                        device_config.storage.units.abbreviate()
                    );
                    let fvm_tool = config
                        .get_host_tool(config::FVM_HOST_TOOL)
                        .await
                        .context("cannot locate fvm tool")?;
                    let resize_result = Command::new(fvm_tool)
                        .arg(&fvm_path)
                        .arg("extend")
                        .arg("--length")
                        .arg(&image_size)
                        .output()?;

                    if !resize_result.status.success() {
                        bail!("Error resizing fvm: {}", str::from_utf8(&resize_result.stderr)?);
                    }
                }
                Some(fvm_path)
            }
            None => None,
        };

        updated_guest.kernel_image = kernel_path;
        updated_guest.zbi_image = zbi_path;
        updated_guest.fvm_image = fvm_path;
        Ok(updated_guest)
    }

    async fn embed_authorized_keys(
        src: &PathBuf,
        dest: &PathBuf,
        config: &FfxConfigWrapper,
    ) -> Result<()> {
        let zbi_tool =
            config.get_host_tool(config::ZBI_HOST_TOOL).await.context("ZBI tool is missing")?;
        let ssh_keys = SshKeyFiles::load().await.context("finding ssh authorized_keys file.")?;
        ssh_keys.create_keys_if_needed().context("create ssh keys if needed")?;
        let auth_keys = ssh_keys.authorized_keys.display().to_string();
        if !ssh_keys.authorized_keys.exists() {
            bail!("No authorized_keys found to configure emulator. {} does not exist.", auth_keys);
        }
        if src == dest {
            return Err(anyhow!("source and dest zbi paths cannot be the same."));
        }

        let replace_str = format!("data/ssh/authorized_keys={}", auth_keys);

        let auth_keys_output = Command::new(zbi_tool)
            .arg("-o")
            .arg(dest)
            .arg("--replace")
            .arg(src)
            .arg("-e")
            .arg(replace_str)
            .output()?;

        if !auth_keys_output.status.success() {
            bail!("Error embedding authorized_keys: {}", str::from_utf8(&auth_keys_output.stderr)?);
        }
        Ok(())
    }

    fn validate_network_flags(&self, emu_config: &EmulatorConfiguration) -> Result<()> {
        match emu_config.host.networking {
            NetworkingMode::None => {
                // Check for console/monitor.
                if emu_config.runtime.console == ConsoleType::None {
                    bail!(
                        "Running without networking enabled and no interactive console;\n\
                        there will be no way to communicate with this emulator.\n\
                        Restart with --console/--monitor or with networking enabled to proceed."
                    );
                }
            }
            NetworkingMode::Auto => {
                // Shouldn't be possible to land here.
                bail!("Networking mode is unresolved after configuration.");
            }
            NetworkingMode::Tap => {
                // MacOS tun/tap is unsupported.
                if std::env::consts::OS == "macos" {
                    eprintln!(
                        "Tun/Tap networking mode is not currently supported on MacOS. \
                        You may experience errors with your current configuration."
                    );
                } else if !tap_available() {
                    eprintln!("To use emu with networking on Linux, configure Tun/Tap:");
                    eprintln!(
                        "  sudo ip tuntap add dev qemu mode tap user $USER && sudo ip link set qemu up"
                    );
                    bail!("Configure Tun/Tap on your host or try --net user.")
                }
            }
            NetworkingMode::User => (),
        }
        Ok(())
    }

    async fn stage(
        emu_config: &mut EmulatorConfiguration,
        ffx_config: &FfxConfigWrapper,
    ) -> Result<()> {
        let name = &emu_config.runtime.name;
        let guest = &emu_config.guest;
        let device = &emu_config.device;
        let reuse = emu_config.runtime.reuse;
        emu_config.guest = Self::stage_image_files(name, guest, device, reuse, ffx_config)
            .await
            .context("could not stage image files")?;

        // This is done to avoid running emu in the same directory as the kernel or other files
        // that are used by qemu. If the multiboot.bin file is in the current directory, it does
        // not start correctly. This probably could be temporary until we know the images loaded
        // do not have files directly in $sdk_root.
        env::set_current_dir(&emu_config.runtime.instance_directory.parent().unwrap())
            .context("problem changing directory to instance dir")?;

        emu_config.flags = process_flag_template(emu_config)
            .context("Failed to process the flags template file.")?;

        Ok(())
    }

    async fn run(
        &mut self,
        mut emulator_cmd: Command,
        proxy: &ffx::TargetCollectionProxy,
    ) -> Result<i32> {
        if self.emu_config().runtime.console == ConsoleType::None {
            let stdout = File::create(&self.emu_config().host.log)
                .context(format!("Couldn't open log file {:?}", &self.emu_config().host.log))?;
            let stderr = stdout
                .try_clone()
                .context("Failed trying to clone stdout for the emulator process.")?;
            emulator_cmd.stdout(stdout).stderr(stderr);
            println!("Logging to {:?}", &self.emu_config().host.log);
        }

        // If using TAP, check for an upscript to run.
        if let Some(script) = match &self.emu_config().host.networking {
            NetworkingMode::Tap => &self.emu_config().runtime.upscript,
            _ => &None,
        } {
            let status = Command::new(&script)
                // TODO(fxbug.dev/100022): Make this configurable.
                // The interface name, "qemu", provided here used to be provided by the qemu
                // executable when it was left for qemu to start the upscript. Since we need
                // to time the sudo prompt and the socket creation, we now launch the script
                // ourselves, so we also have to provide the interface name. This will likely
                // need to eventually be configurable to support running emulators on
                // multiple tap interfaces.
                .arg("qemu")
                .status()?;
            if !status.success() {
                return Err(anyhow!(
                    "Upscript {} returned non-zero exit code {}",
                    script.display(),
                    status.code().map_or("None".to_string(), |v| format!("{}", v))
                ));
            }
        }

        let shared_process = SharedChild::spawn(&mut emulator_cmd)?;
        let child_arc = Arc::new(shared_process);

        self.set_pid(child_arc.id());

        self.write_to_disk(&self.emu_config().runtime.instance_directory)
            .context("Failed to write the emulation configuration file to disk.")?;

        let ssh = self.emu_config().host.port_map.get("ssh");
        let ssh_port = if let Some(ssh) = ssh { ssh.host } else { None };
        if self.emu_config().host.networking == NetworkingMode::User {
            // We only need to do this if we're running in user net mode.
            if let Some(ssh_port) = ssh_port {
                add_target(proxy, ssh_port)
                    .await
                    .context("Failed to add the emulator to the ffx target collection.")?;
            }
        }

        if self.emu_config().runtime.debugger {
            println!("The emulator will wait for a debugger to attach before starting up.");
            println!("Attach to process {} to continue launching the emulator.", self.get_pid());
        }

        if self.emu_config().runtime.console == ConsoleType::Monitor
            || self.emu_config().runtime.console == ConsoleType::Console
        {
            // When running with '--monitor' or '--console' mode, the user is directly interacting
            // with the emulator console, or the guest console. Therefore wait until the
            // execution of QEMU or AEMU terminates.
            match fuchsia_async::unblock(move || process::monitored_child_process(&child_arc)).await
            {
                Ok(_) => {
                    return Ok(0);
                }
                Err(e) => {
                    let running = self.is_running();
                    let pid = self.get_pid();
                    let target_id = self.emu_config().runtime.name.clone();
                    if let Some(stop_error) =
                        Self::stop_emulator(running, pid, &target_id, proxy).await.err()
                    {
                        tracing::debug!(
                            "Error encountered in stop when handling failed launch: {:?}",
                            stop_error
                        );
                    }
                    ffx_bail!("Emulator launcher did not terminate properly, error: {}", e)
                }
            }
        } else if !self.emu_config().runtime.startup_timeout.is_zero() {
            // Wait until the emulator is considered "active" before returning to the user.
            let mut time_left = self.emu_config().runtime.startup_timeout;
            print!("Waiting for Fuchsia to start (up to {} seconds).", &time_left.as_secs());
            tracing::debug!(
                "Waiting for Fuchsia to start (up to {} seconds)...",
                &time_left.as_secs()
            );
            let name = &self.emu_config().runtime.name;
            while !time_left.is_zero() {
                if is_active(proxy, name).await {
                    println!("\nEmulator is ready.");
                    tracing::debug!("Emulator is ready.");
                    break;
                } else {
                    // Output a little status indicator to show we haven't gotten stuck.
                    // Note that we discard the result on the flush call; it's not important enough
                    // that we flushed the output stream to derail the launch.
                    print!(".");
                    std::io::stdout().flush().ok();

                    // Perform a check to make sure the process is still alive, otherwise report
                    // failure to launch.
                    if !self.is_running() {
                        tracing::error!(
                            "Emulator process failed to launch, but we don't know the cause. \
                            Check the emulator log, or look for a crash log."
                        );
                        eprintln!(
                            "\nEmulator process failed to launch, but we don't know the cause. \
                            Printing the contents of the emulator log...\n"
                        );
                        match dump_log_to_out(&self.emu_config().host.log, &mut stderr()) {
                            Ok(_) => (),
                            Err(e) => eprintln!("Couldn't print the log: {:?}", e),
                        };
                        if self.emu_config().host.networking == NetworkingMode::User {
                            // We only need to do this if we're running in user net mode.
                            if let Some(ssh_port) = ssh_port {
                                if let Err(e) =
                                    remove_target(proxy, &format!("127.0.0.1:{}", ssh_port)).await
                                {
                                    // A failure here probably means it was never added.
                                    // Just log the error and quit.
                                    tracing::warn!(
                                        "Couldn't remove target from ffx during shutdown: {:?}",
                                        e
                                    );
                                }
                            }
                        }
                        return Ok(1);
                    }

                    time_left = time_left.sub(Duration::from_secs(1));
                    if time_left.is_zero() {
                        eprintln!();
                        eprintln!(
                            "After {} seconds, the emulator has not responded to network queries.",
                            self.emu_config().runtime.startup_timeout.as_secs()
                        );
                        if self.is_running() {
                            eprintln!(
                                "The emulator process is still running (pid {}).",
                                self.get_pid()
                            );
                            eprintln!(
                                "The emulator is configured to {} network access.",
                                match self.emu_config().host.networking {
                                    NetworkingMode::Tap => "use tun/tap-based",
                                    NetworkingMode::User => "use user-mode/port-mapped",
                                    NetworkingMode::None => "disable all",
                                    NetworkingMode::Auto => bail!(
                                        "Auto Networking mode should not be possible after staging \
                                        is complete. Configuration is corrupt; bailing out."
                                    ),
                                }
                            );
                            eprintln!(
                                "Hardware acceleration is {}.",
                                if self.emu_config().host.acceleration == AccelerationMode::Hyper {
                                    "enabled"
                                } else {
                                    "disabled, which significantly slows down the emulator"
                                }
                            );
                            eprintln!(
                                "You can execute `ffx target list` to keep monitoring the device, \
                                or `ffx emu stop` to terminate it."
                            );
                            eprintln!(
                                "You can also change the timeout if you keep encountering this \
                                message by executing `ffx config set emu.start.uptime <seconds>`."
                            );
                        } else {
                            eprintln!();
                            eprintln!(
                                "Emulator process failed to launch, but we don't know the cause. \
                                Printing the contents of the emulator log...\n"
                            );
                            match dump_log_to_out(
                                &self.emu_config().host.log,
                                &mut std::io::stderr(),
                            ) {
                                Ok(_) => (),
                                Err(e) => eprintln!("Couldn't print the log: {:?}", e),
                            };
                        }

                        tracing::warn!(
                            "Emulator did not respond to a health check before timing out."
                        );
                    }
                }
            }
        }
        Ok(0)
    }

    /// The parameters here may be a bit unintuitive: because stop_emulator is called from
    /// run(), it can't receive "self" as a parameter. Since both are running async (required for
    /// calls to add_target/remove_target), they run in separate threads, and self can't be safely
    /// shared across threads. Instead, we pull only those variables we need for stop out of
    /// "self" and pass them in explicitly.
    ///
    /// running:    Boolean to indicate that the engine specified is active.
    ///             Typically comes from `engine::is_running();`.
    /// pid:        The process ID of the running emulator; used to send a signal to the process to
    ///             cause termination.
    /// target_id:  This is the engine name, used to issue a `ffx target remove` command.
    /// proxy:      The interface to the `ffx target` backend, provided by the ffx front-end as a
    ///             parameter to the plugin subcommands. Used to issue a `ffx target remove`
    ///             command.
    async fn stop_emulator(
        running: bool,
        pid: u32,
        target_id: &str,
        proxy: &ffx::TargetCollectionProxy,
    ) -> Result<()> {
        if let Err(e) = remove_target(proxy, target_id).await {
            // Even if we can't remove it, still continue shutting down.
            tracing::warn!("Couldn't remove target from ffx during shutdown: {:?}", e);
        }
        if running {
            println!("Terminating running instance {:?}", pid);
            if let Some(terminate_error) = process::terminate(pid).err() {
                tracing::warn!("Error encountered terminating process: {:?}", terminate_error);
            }
        }
        Ok(())
    }

    /// Access to the engine's pid field.
    fn set_pid(&mut self, pid: u32);
    fn get_pid(&self) -> u32;
}

#[cfg(test)]
mod tests {
    use std::io::Read;

    use super::*;
    use async_trait::async_trait;
    use ffx_config::{query, ConfigLevel};
    use ffx_emulator_config::EngineType;
    use serde::Serialize;
    use serde_json::json;
    use tempfile::{tempdir, TempDir};

    #[derive(Default, Serialize)]
    struct TestEngine {}
    impl QemuBasedEngine for TestEngine {
        fn set_pid(&mut self, _pid: u32) {}
        fn get_pid(&self) -> u32 {
            todo!()
        }
    }
    #[async_trait]
    impl EmulatorEngine for TestEngine {
        async fn start(&mut self, _: Command, _: &ffx::TargetCollectionProxy) -> Result<i32> {
            todo!()
        }
        async fn stop(&self, _: &ffx::TargetCollectionProxy) -> Result<()> {
            todo!()
        }
        fn show(&self) {
            todo!()
        }
        async fn stage(&mut self) -> Result<()> {
            todo!()
        }
        fn validate(&self) -> Result<()> {
            todo!()
        }
        fn engine_type(&self) -> EngineType {
            EngineType::default()
        }
        fn is_running(&self) -> bool {
            false
        }
        fn build_emulator_cmd(&self) -> Command {
            todo!()
        }
        fn emu_config(&self) -> &EmulatorConfiguration {
            todo!()
        }
        fn emu_config_mut(&mut self) -> &mut EmulatorConfiguration {
            todo!()
        }
    }
    impl SerializingEngine for TestEngine {}

    const ORIGINAL: &str = "THIS_STRING";
    const UPDATED: &str = "THAT_VALUE*";

    async fn setup(
        config: &mut FfxConfigWrapper,
        guest: &mut GuestConfig,
        temp: &TempDir,
    ) -> Result<PathBuf> {
        let root = temp.path();

        let kernel_path = root.join("kernel");
        let zbi_path = root.join("zbi");
        let fvm_path = root.join("fvm");

        let _ = fs::File::options()
            .write(true)
            .create(true)
            .open(&kernel_path)
            .context("cannot create test kernel file")?;
        let _ = fs::File::options()
            .write(true)
            .create(true)
            .open(&zbi_path)
            .context("cannot create test zbi file")?;
        let _ = fs::File::options()
            .write(true)
            .create(true)
            .open(&fvm_path)
            .context("cannot create test fvm file")?;

        config.overrides.insert(config::FVM_HOST_TOOL, "echo".to_string());
        config.overrides.insert(config::ZBI_HOST_TOOL, "echo".to_string());
        config.overrides.insert(config::EMU_INSTANCE_ROOT_DIR, root.display().to_string());

        guest.kernel_image = kernel_path;
        guest.zbi_image = zbi_path;
        guest.fvm_image = Some(fvm_path);

        // Set the paths to use for the SSH keys
        query("ssh.pub")
            .level(Some(ConfigLevel::User))
            .set(json!([root.join("test_authorized_keys")]))
            .await?;
        query("ssh.priv")
            .level(Some(ConfigLevel::User))
            .set(json!([root.join("test_ed25519_key")]))
            .await?;

        Ok(PathBuf::from(root))
    }

    fn write_to(path: &PathBuf, value: &str) -> Result<()> {
        println!("Writing {} to {}", value, path.display());
        let mut file = File::options()
            .write(true)
            .open(path)
            .context(format!("cannot open existing file for write: {}", path.display()))?;
        File::write(&mut file, value.as_bytes())
            .context(format!("cannot write buffer to file: {}", path.display()))?;

        Ok(())
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_staging_no_reuse() -> Result<()> {
        let _env = ffx_config::test_init().await?;
        let temp = tempdir().context("cannot get tempdir")?;
        let mut config = FfxConfigWrapper::new();
        let instance_name = "test-instance";
        let mut guest = GuestConfig::default();
        let device = DeviceConfig::default();

        let root = setup(&mut config, &mut guest, &temp).await?;

        write_to(&guest.kernel_image, ORIGINAL)
            .context("cannot write original value to kernel file")?;
        write_to(guest.fvm_image.as_ref().unwrap(), ORIGINAL)
            .context("cannot write original value to fvm file")?;

        let updated = <TestEngine as QemuBasedEngine>::stage_image_files(
            instance_name,
            &guest,
            &device,
            false,
            &config,
        )
        .await;

        assert!(updated.is_ok(), "expected OK got {:?}", updated.unwrap_err());

        let actual = updated.context("cannot get updated guest config")?;
        let expected = GuestConfig {
            kernel_image: root.join(instance_name).join("kernel"),
            zbi_image: root.join(instance_name).join("zbi"),
            fvm_image: Some(root.join(instance_name).join("fvm")),
        };
        assert_eq!(actual, expected);

        // Test no reuse when old files exist. The original files should be overwritten.
        write_to(&guest.kernel_image, UPDATED)
            .context("cannot write updated value to kernel file")?;
        write_to(guest.fvm_image.as_ref().unwrap(), UPDATED)
            .context("cannot write updated value to fvm file")?;

        let updated = <TestEngine as QemuBasedEngine>::stage_image_files(
            instance_name,
            &guest,
            &device,
            false,
            &config,
        )
        .await;

        assert!(updated.is_ok(), "expected OK got {:?}", updated.unwrap_err());

        let actual = updated.context("cannot get updated guest config, reuse")?;
        let expected = GuestConfig {
            kernel_image: root.join(instance_name).join("kernel"),
            zbi_image: root.join(instance_name).join("zbi"),
            fvm_image: Some(root.join(instance_name).join("fvm")),
        };
        assert_eq!(actual, expected);

        println!("Reading contents from {}", actual.kernel_image.display());
        println!("Reading contents from {}", actual.fvm_image.as_ref().unwrap().display());
        let mut kernel = File::open(&actual.kernel_image)
            .context("cannot open overwritten kernel file for read")?;
        let mut fvm = File::open(&actual.fvm_image.unwrap())
            .context("cannot open overwritten fvm file for read")?;

        let mut kernel_contents = String::new();
        let mut fvm_contents = String::new();

        kernel
            .read_to_string(&mut kernel_contents)
            .context("cannot read contents of reused kernel file")?;
        fvm.read_to_string(&mut fvm_contents).context("cannot read contents of reused fvm file")?;

        assert_eq!(kernel_contents, UPDATED);
        assert_eq!(fvm_contents, UPDATED);

        Ok(())
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_staging_with_reuse() -> Result<()> {
        let _env = ffx_config::test_init().await?;
        let temp = tempdir().context("cannot get tempdir")?;
        let mut config = FfxConfigWrapper::new();
        let instance_name = "test-instance";
        let mut guest = GuestConfig::default();
        let device = DeviceConfig::default();

        let root = setup(&mut config, &mut guest, &temp).await?;

        // This checks if --reuse is true, but the directory isn't there to reuse; should succeed.
        write_to(&guest.kernel_image, ORIGINAL)
            .context("cannot write original value to kernel file")?;
        write_to(guest.fvm_image.as_ref().unwrap(), ORIGINAL)
            .context("cannot write original value to fvm file")?;

        let updated: Result<GuestConfig> = <TestEngine as QemuBasedEngine>::stage_image_files(
            instance_name,
            &guest,
            &device,
            true,
            &config,
        )
        .await;

        assert!(updated.is_ok(), "expected OK got {:?}", updated.unwrap_err());

        let actual = updated.context("cannot get updated guest config")?;
        let expected = GuestConfig {
            kernel_image: root.join(instance_name).join("kernel"),
            zbi_image: root.join(instance_name).join("zbi"),
            fvm_image: Some(root.join(instance_name).join("fvm")),
        };
        assert_eq!(actual, expected);

        // Test reuse. Note that the ZBI file isn't actually copied in the test, since we replace
        // the ZBI tool with an "echo" command.
        write_to(&guest.kernel_image, UPDATED)
            .context("cannot write updated value to kernel file")?;
        write_to(guest.fvm_image.as_ref().unwrap(), UPDATED)
            .context("cannot write updated value to fvm file")?;

        let updated = <TestEngine as QemuBasedEngine>::stage_image_files(
            instance_name,
            &guest,
            &device,
            true,
            &config,
        )
        .await;

        assert!(updated.is_ok(), "expected OK got {:?}", updated.unwrap_err());

        let actual = updated.context("cannot get updated guest config, reuse")?;
        let expected = GuestConfig {
            kernel_image: root.join(instance_name).join("kernel"),
            zbi_image: root.join(instance_name).join("zbi"),
            fvm_image: Some(root.join(instance_name).join("fvm")),
        };
        assert_eq!(actual, expected);

        println!("Reading contents from {}", actual.kernel_image.display());
        let mut kernel =
            File::open(&actual.kernel_image).context("cannot open reused kernel file for read")?;
        let mut fvm = File::open(&actual.fvm_image.unwrap())
            .context("cannot open reused fvm file for read")?;

        let mut kernel_contents = String::new();
        let mut fvm_contents = String::new();

        kernel
            .read_to_string(&mut kernel_contents)
            .context("cannot read contents of reused kernel file")?;
        fvm.read_to_string(&mut fvm_contents).context("cannot read contents of reused fvm file")?;

        assert_eq!(kernel_contents, ORIGINAL);
        assert_eq!(fvm_contents, ORIGINAL);

        Ok(())
    }

    #[test]
    fn test_validate_net() -> Result<()> {
        // User mode doesn't have specific requirements, so it should return OK.
        let engine = TestEngine::default();
        let mut emu_config = EmulatorConfiguration::default();
        emu_config.host.networking = NetworkingMode::User;
        let result = engine.validate_network_flags(&emu_config);
        assert!(result.is_ok(), "{:?}", result.unwrap_err());

        // No networking returns an error if no console is selected.
        emu_config.host.networking = NetworkingMode::None;
        emu_config.runtime.console = ConsoleType::None;
        let result = engine.validate_network_flags(&emu_config);
        assert!(result.is_err());

        emu_config.runtime.console = ConsoleType::Console;
        let result = engine.validate_network_flags(&emu_config);
        assert!(result.is_ok(), "{:?}", result.unwrap_err());

        emu_config.runtime.console = ConsoleType::Monitor;
        let result = engine.validate_network_flags(&emu_config);
        assert!(result.is_ok(), "{:?}", result.unwrap_err());

        // Tap mode errors if host is Linux and there's no interface, but we can't mock the
        // interface, so we can't test this case yet.
        emu_config.host.networking = NetworkingMode::Tap;

        // Validation runs after configuration is merged with values from PBMs and runtime, so Auto
        // values should already be resolved. If not, that's a failure.
        emu_config.host.networking = NetworkingMode::Auto;
        let result = engine.validate_network_flags(&emu_config);
        assert!(result.is_err());

        Ok(())
    }
}
