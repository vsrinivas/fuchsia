// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::portpicker::{pick_unused_port, Port};
use crate::target;
use crate::types::{
    get_sdk_data_dir, read_env_path, HostTools, ImageFiles, InTreePaths, SSHKeys, VDLArgs,
};
use crate::vdl_proto_parser::{get_emu_pid, get_ssh_port};
use ansi_term::Colour::*;
use anyhow::Result;
use errors::ffx_bail;
use ffx_emulator_args::{KillCommand, StartCommand};
use fidl_fuchsia_developer_bridge as bridge;
use regex::Regex;
use shared_child::SharedChild;
use signal_hook;
use std::env;
use std::fs::{copy, File};
use std::io::Write;
use std::path::PathBuf;
use std::process::{Command, Output};
use std::str;
use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::Arc;
use std::thread;
use std::time;
use tempfile::{Builder, TempDir};

mod remote;

static ANALYTICS_ENV_VAR: &str = "FVDL_INVOKER";

/// Monitors a shared process for the interrupt signal. Only used for --monitor or --emu-only modes.
///
/// If user runs with --montior or --emu-only, Fuchsia Emulator will be running in the foreground,
/// here we listen for the interrupt signal (ctrl+c), once detected, we'll wait for the emulator
/// process to finish.
fn monitored_child_process(child_arc: &Arc<SharedChild>) -> Result<()> {
    let child_arc_clone = child_arc.clone();
    let term = Arc::new(AtomicBool::new(false));
    signal_hook::flag::register(signal_hook::consts::SIGINT, Arc::clone(&term))?;
    let thread = std::thread::spawn(move || {
        while !term.load(Ordering::Relaxed) {
            thread::sleep(time::Duration::from_secs(1));
        }
        child_arc_clone.wait().unwrap()
    });
    thread.join().expect("cannot join monitor thread");
    Ok(())
}
pub struct VDLFiles {
    image_files: ImageFiles,
    host_tools: HostTools,
    ssh_files: SSHKeys,

    /// A temp directory to stage required files used to start emulator.
    staging_dir: TempDir,

    /// A file created under staging_dir that stores vdl proto output.
    output_proto: PathBuf,

    /// A file created under staging_dir to store emulator output.
    emulator_log: PathBuf,

    /// True if user invoked fvdl with the arg --sdk.
    is_sdk: bool,

    /// True to enable extra logging.
    verbose: bool,
}

impl VDLFiles {
    pub fn new(is_sdk: bool, verbose: bool) -> Result<VDLFiles> {
        let staging_dir = Builder::new().prefix("vdl_staging_").tempdir()?;
        let staging_dir_path = staging_dir.path().to_owned();
        let vdl_files;
        if is_sdk {
            vdl_files = VDLFiles {
                image_files: ImageFiles::from_sdk_env()?,
                host_tools: HostTools::from_sdk_env()?,
                ssh_files: SSHKeys::from_sdk_env()?,
                output_proto: staging_dir_path.join("vdl_proto"),
                emulator_log: staging_dir_path.join("emu_log"),
                staging_dir: staging_dir,
                is_sdk: is_sdk,
                verbose: verbose,
            };
        } else {
            let mut in_tree = InTreePaths { root_dir: None, build_dir: None };
            vdl_files = VDLFiles {
                image_files: ImageFiles::from_tree_env(&mut in_tree)?,
                host_tools: HostTools::from_tree_env(&mut in_tree)?,
                ssh_files: SSHKeys::from_tree_env(&mut in_tree)?,
                output_proto: staging_dir_path.join("vdl_proto"),
                emulator_log: staging_dir_path.join("emu_log"),
                staging_dir: staging_dir,
                is_sdk: is_sdk,
                verbose: verbose,
            };
        }
        if verbose {
            println!("{:#?}", vdl_files.image_files);
            println!("{:#?}", vdl_files.ssh_files);
            println!("{:#?}", vdl_files.host_tools);
        }
        Ok(vdl_files)
    }

    fn provision_zbi(&self) -> Result<PathBuf> {
        let zbi_out = self.staging_dir.path().join("femu_zircona-ed25519");
        let status = Command::new(&self.host_tools.zbi)
            .arg(format!(
                "--compressed={}",
                env::var("FUCHSIA_ZBI_COMPRESSION").unwrap_or("zstd".to_string())
            ))
            .arg("-o")
            .arg(&zbi_out)
            .arg(&self.image_files.zbi)
            .arg("--entry")
            .arg(format!("data/ssh/authorized_keys={}", self.ssh_files.authorized_keys.display()))
            .status()?;
        if status.success() {
            if self.verbose {
                println!("[fvdl] provisioned zbi {:?}", zbi_out);
            }
            Ok(zbi_out)
        } else {
            ffx_bail!("Cannot provision zbi. Exit status was {}", status.code().unwrap_or_default())
        }
    }

    fn assemble_system_images(&self, sdk_version: &String) -> Result<String> {
        if sdk_version.is_empty() {
            // When SDK version is not specified, then all required files already exists locally (i.e in-tree)
            Ok(format!(
                "{},{},{},{},{},{},{}",
                self.ssh_files.private_key.display(),
                self.ssh_files.authorized_keys.display(),
                self.provision_zbi()?.display(),
                self.image_files.kernel.display(),
                self.image_files.amber_files.as_ref().unwrap_or(&PathBuf::new()).display(),
                self.image_files.build_args.as_ref().unwrap_or(&PathBuf::new()).display(),
                self.image_files.fvm.as_ref().unwrap_or(&PathBuf::new()).display(),
            ))
        } else {
            // Not specifying any image files will allow device_launcher to download from GCS.
            Ok(format!(
                "{},{}",
                self.ssh_files.private_key.display(),
                self.ssh_files.authorized_keys.display(),
            ))
        }
    }

    fn generate_fvd(&self, window_width: &usize, window_height: &usize) -> Result<PathBuf> {
        // Note the value for ram should match the defaults used in `fx emu` (//tools/devshell/emu)
        // and in `fx qemu` (//zircon/scripts/run-zircon).
        let data = format!(
            "device_spec {{
  horizontal_resolution: {}
  vertical_resolution: {}
  vm_heap: 192
  ram: 8192
  cache: 32
  screen_density: 240
}}
",
            window_width, window_height
        );
        let fvd_proto = self.staging_dir.path().join("virtual_device.textproto");
        File::create(&fvd_proto)?.write_all(data.as_bytes())?;
        Ok(fvd_proto)
    }

    // Notes for usage in SDK (ex: `fvdl --sdk ...`)
    // If `--aemu-path` is specified, use that.
    // Else If `--aemu-version` is specified, download aemu with that version label from cipd.
    // Else If `<sdk_root>/bin/aemu.version` is present, download aemu using that version from cipd.
    // Else download aemu using version `integration` from cipd.
    //
    // Notes for usage in-tree (ex: `fx vdl start ...`)
    // If `--aemu-path` is specified, use that.
    // Else If `--aemu-version` is specified, download aemu with that version label from cipd.
    // Else If env_var ${PREBUILT_AEMU_DIR} is set (in-tree default), use that.
    // Else download aemu using version `integration` from cipd.
    pub fn resolve_aemu_path(&self, start_command: &StartCommand) -> Result<PathBuf> {
        match &start_command.aemu_path {
            Some(aemu_path) => Ok(PathBuf::from(aemu_path)),
            None => {
                let aemu_cipd_version = match &start_command.aemu_version {
                    Some(version) => version.clone(),
                    None => {
                        if self.host_tools.aemu.as_os_str().is_empty() {
                            self.host_tools
                                .read_prebuild_version("aemu.version")
                                .unwrap_or(String::from("integration"))
                        } else {
                            return Ok(self.host_tools.aemu.clone());
                        }
                    }
                };
                Ok(self
                    .host_tools
                    .download_and_extract(
                        aemu_cipd_version.to_string(),
                        "third_party/aemu".to_string(),
                    )?
                    .join("emulator"))
            }
        }
    }

    pub fn resolve_grpcwebproxy_path(&self, start_command: &StartCommand) -> Result<PathBuf> {
        match &start_command.grpcwebproxy_path {
            Some(grpcwebproxy_path) => Ok(PathBuf::from(grpcwebproxy_path)),
            None => {
                let grpcwebproxy_cipd_version = match &start_command.grpcwebproxy_version {
                    Some(version) => version.clone(),
                    None => {
                        if self.host_tools.grpcwebproxy.as_os_str().is_empty() {
                            self.host_tools
                                .read_prebuild_version("grpcwebproxy.version")
                                .unwrap_or(String::from("latest"))
                        } else {
                            return Ok(self.host_tools.grpcwebproxy.clone());
                        }
                    }
                };
                Ok(self
                    .host_tools
                    .download_and_extract(
                        grpcwebproxy_cipd_version.to_string(),
                        "third_party/grpcwebproxy".to_string(),
                    )?
                    .join("grpcwebproxy"))
            }
        }
    }

    pub fn resolve_vdl_path(&self, start_command: &StartCommand) -> Result<PathBuf> {
        match &start_command.vdl_path {
            Some(vdl_path) => Ok(PathBuf::from(vdl_path)),
            None => {
                let vdl_cipd_version = match &start_command.vdl_version {
                    Some(version) => version.clone(),
                    None => {
                        if self.host_tools.vdl.as_os_str().is_empty() {
                            self.host_tools
                                .read_prebuild_version("device_launcher.version")
                                .unwrap_or(String::from("latest"))
                        } else {
                            return Ok(self.host_tools.vdl.clone());
                        }
                    }
                };
                Ok(self
                    .host_tools
                    .download_and_extract(vdl_cipd_version.to_string(), "vdl".to_string())?
                    .join("device_launcher"))
            }
        }
    }

    // Checks if user has specified a portmap. If portmap is specified, we'll check if ssh port is included.
    // If ssh port is not included, we'll pick a port and forward that together with the rest of portmap.
    pub fn resolve_portmap(&self, start_command: &StartCommand) -> (String, u16) {
        let mut ssh_port = 0;
        match &start_command.port_map {
            Some(port_map) => {
                let re = Regex::new(r"::(?P<ssh>\d+)-:22(,|$)").unwrap();
                re.captures(port_map).and_then(|cap| {
                    cap.name("ssh").map(|ssh| ssh_port = ssh.as_str().parse::<u16>().unwrap())
                });
                if ssh_port == 0 {
                    ssh_port = pick_unused_port().unwrap();
                    (format!("{},hostfwd=tcp::{}-:22", port_map.clone(), ssh_port), ssh_port)
                } else {
                    (port_map.clone(), ssh_port)
                }
            }
            None => {
                ssh_port = pick_unused_port().unwrap();
                (format!("hostfwd=tcp::{}-:22", ssh_port), ssh_port)
            }
        }
    }

    // Sets the label used in analytics. If running in automated testing environment, user can create
    // a custom label to track usage by setting the environment variable FVDL_INVOKER.
    pub fn resolve_invoker(&self) -> String {
        match env::var_os(ANALYTICS_ENV_VAR) {
            Some(v) => return String::from(v.to_str().unwrap_or_default()),
            None => {
                if self.is_sdk {
                    return String::from("fvdl-sdk");
                }
                return String::from("fvdl-intree");
            }
        }
    }

    pub fn check_start_command(&self, command: &StartCommand) -> Result<()> {
        // TODO(fxb/82804) Remove after a month.
        if command.nopackageserver {
            println!(
                "{}",
                Yellow
                    .paint("WARNING: --nopackageserver will be removed soon, the default behavior no longer starts package server.")
            );
        }
        if command.nointeractive && command.vdl_output.is_none() {
            ffx_bail!(
                "--vdl-ouput must be specified for --nointeractive mode.\n\
                example: fx vdl start --nointeractive --vdl-output /path/to/saved/output.log\n\
                example: ./fvdl --sdk start --nointeractive --vdl-output /path/to/saved/output.log\n"
            )
        }
        // Check that build architecture is specified when overriding image files
        if (command.amber_files.is_some()
            || command.fvm_image.is_some()
            || command.kernel_image.is_some()
            || command.zbi_image.is_some())
            && command.image_architecture.is_none()
        {
            ffx_bail!(
                "--image-architecture must be specified in order to override image files.\n\
            accepted values are 'arm64' and 'x64'.
            example: fx vdl start --image-architecture x64 --kernel-image /path/to/kernel \n\
            example: ./fvdl --sdk start --image-architecture x64 --kernel-image /path/to/kernel \n"
            )
        }
        // At a minimum, zbi and kernel images must be both specified in order to boot up the emulator.
        if (command.kernel_image.is_some() && command.zbi_image.is_none())
            || (command.kernel_image.is_none() && command.zbi_image.is_some())
        {
            ffx_bail!("--kernel-image and --zbi-image must both be specified in order to override fuchsia image used for emulator.\n\
            You can optionally specify --amber-files and --fvm-image locations. \n
        ")
        }
        Ok(())
    }

    /// Launches FEMU, opens an SSH session, and waits for the FEMU instance or SSH session to exit.
    pub async fn start_emulator(
        &mut self,
        start_command: &StartCommand,
        daemon_proxy: Option<&bridge::DaemonProxy>,
    ) -> Result<i32> {
        self.check_start_command(&start_command)?;
        let vdl_args: VDLArgs = start_command.clone().into();

        let mut gcs_image = vdl_args.gcs_image_archive;
        let mut gcs_bucket = vdl_args.gcs_bucket;
        let mut sdk_version = vdl_args.sdk_version;

        if vdl_args.cache_root.to_str().unwrap_or("") != "" {
            println!("[fvdl] using cached image files");
            self.image_files.update_paths_from_cache(&vdl_args.cache_root);
        }

        // overriding image files via args will make cache a no-op
        self.image_files.update_paths_from_args(&start_command);
        self.ssh_files.update_paths_from_args(&start_command);

        // If minimum required image files are specified & exist, skip download by clearing out gcs related flags even
        // if user has specified --sdk-version etc.
        if self.image_files.images_exist() {
            gcs_image = String::from("");
            gcs_bucket = String::from("");
            sdk_version = String::from("");
            self.image_files.stage_files(&self.staging_dir.path().to_owned())?;
        }
        self.ssh_files.stage_files(&self.staging_dir.path().to_owned())?;

        if self.verbose {
            println!("[fvdl] using the following image files to launch emulator:");
            println!("{:?}", self.image_files);
        }

        if !self.is_sdk {
            self.image_files.check()?;
            self.ssh_files.check()?;
        }

        let fvd = match &start_command.device_proto {
            Some(proto) => PathBuf::from(proto),
            None => self.generate_fvd(&start_command.window_width, &start_command.window_height)?,
        };

        let aemu = self.resolve_aemu_path(start_command)?;
        if !aemu.exists() || !aemu.is_file() {
            ffx_bail!("Invalid 'emulator' binary at path {}", aemu.display())
        }

        let vdl = self.resolve_vdl_path(start_command)?;
        if !vdl.exists() || !vdl.is_file() {
            ffx_bail!("device_launcher binary cannot be found at {}", vdl.display())
        }

        let mut grpcwebproxy = self.host_tools.grpcwebproxy.clone();
        if vdl_args.enable_grpcwebproxy {
            grpcwebproxy = self.resolve_grpcwebproxy_path(start_command)?;
            if !grpcwebproxy.exists() || !grpcwebproxy.is_file() {
                ffx_bail!("grpcwebproxy binary cannot be found at {}", grpcwebproxy.display())
            }
        }

        let emu_log = start_command
            .emulator_log
            .as_ref()
            .map_or(self.emulator_log.clone(), |v| PathBuf::from(v));

        let package_server_log =
            start_command.package_server_log.as_ref().map_or(PathBuf::new(), |v| PathBuf::from(v));

        if let Some(location) = &start_command.vdl_output {
            self.output_proto = PathBuf::from(location);
        }

        let (port_map, ssh_port) = self.resolve_portmap(&start_command);

        // Enable emulator grpc server if running on linux
        // doc: https://android.googlesource.com/platform/external/qemu/+/refs/heads/emu-master-dev/android/android-grpc/docs
        let enable_emu_controller = match env::consts::OS {
            "linux" => true,
            _ => false,
        };

        let invoker = self.resolve_invoker();
        let mut cmd = Command::new(&vdl);
        cmd.arg("--action=start")
            .arg("--emulator_binary_path")
            .arg(&aemu)
            .arg("--pm_tool")
            .arg(&self.host_tools.pm.as_ref().unwrap_or(&PathBuf::new()))
            .arg("--far_tool")
            .arg(&self.host_tools.far.as_ref().unwrap_or(&PathBuf::new()))
            .arg("--fvm_tool")
            .arg(&self.host_tools.fvm.as_ref().unwrap_or(&PathBuf::new()))
            .arg("--device_finder_tool")
            .arg(&self.host_tools.device_finder)
            .arg("--zbi_tool")
            .arg(&self.host_tools.zbi)
            .arg("--grpcwebproxy_tool")
            .arg(&grpcwebproxy)
            .arg(format!("--system_images={}", self.assemble_system_images(&sdk_version)?))
            .arg("--host_port_map")
            .arg(&port_map)
            .arg("--output_launched_device_proto")
            .arg(&self.output_proto)
            .arg("--emu_log")
            .arg(&emu_log)
            .arg("--package_server_log")
            .arg(&package_server_log)
            .arg("--proto_file_path")
            .arg(&fvd)
            .arg("--audio=true")
            .arg(format!("--event_action={}", &invoker))
            .arg(format!("--debugger={}", &start_command.debugger))
            .arg(format!("--monitor={}", &start_command.monitor))
            .arg(format!("--emu_only={}", &start_command.emu_only))
            .arg(format!("--resize_fvm={}", vdl_args.image_size))
            .arg(format!("--gpu={}", vdl_args.gpu))
            .arg(format!("--headless_mode={}", vdl_args.headless))
            .arg(format!("--tuntap={}", vdl_args.tuntap))
            .arg(format!("--upscript={}", vdl_args.upscript))
            .arg(format!("--start_package_server={}", vdl_args.start_package_server))
            .arg(format!("--serve_packages={}", vdl_args.packages_to_serve))
            .arg(format!("--package_server_port={}", vdl_args.package_server_port))
            .arg(format!("--unpack_repo_root={}", vdl_args.amber_unpack_root))
            .arg(format!("--pointing_device={}", vdl_args.pointing_device))
            .arg(format!("--enable_webrtc={}", vdl_args.enable_grpcwebproxy))
            .arg(format!("--grpcwebproxy_port={}", vdl_args.grpcwebproxy_port))
            .arg(format!("--gcs_bucket={}", gcs_bucket))
            .arg(format!("--image_archive={}", gcs_image))
            .arg(format!("--build_id={}", sdk_version))
            .arg(format!("--enable_emu_controller={}", enable_emu_controller))
            .arg(format!("--hidpi_scaling={}", vdl_args.enable_hidpi_scaling))
            .arg(format!("--image_cache_path={}", vdl_args.cache_root.display()))
            .arg(format!("--kernel_args={}", vdl_args.extra_kerel_args))
            .arg(format!("--accel={}", vdl_args.acceleration))
            .arg(format!("--image_architecture={}", vdl_args.image_architecture));

        for i in 0..start_command.envs.len() {
            cmd.arg("--env").arg(&start_command.envs[i]);
        }
        if self.verbose {
            println!("[fvdl] Running device_launcher cmd: {:?}", cmd);
        }
        let shared_process = SharedChild::spawn(&mut cmd)?;
        let child_arc = Arc::new(shared_process);

        if start_command.emu_only || start_command.monitor {
            // When running with '--emu-only' or '--monitor' mode, the user is directly interacting
            // with the emulator console, the execution ends when either QEMU or AEMU terminates.
            // We don't specify a 'daemon_proxy', because no target will be manually added.
            match monitored_child_process(&child_arc) {
                Ok(_) => {
                    self.stop_vdl(
                        &KillCommand {
                            launched_proto: Some(self.output_proto.display().to_string()),
                            vdl_path: Some(vdl.display().to_string()),
                        },
                        None,
                    )
                    .await?;
                    return Ok(0);
                }
                Err(e) => {
                    self.stop_vdl(
                        &KillCommand {
                            launched_proto: Some(self.output_proto.display().to_string()),
                            vdl_path: Some(vdl.display().to_string()),
                        },
                        None,
                    )
                    .await?;
                    ffx_bail!("emulator launcher did not terminate propertly, error: {}", e)
                }
            }
        }

        let status = child_arc.wait()?;
        if !status.success() {
            if self.emulator_log.exists() {
                let persistent_emu_log = read_env_path("FUCHSIA_OUT_DIR")
                    .unwrap_or(env::current_dir()?)
                    .join("emu_crash.log");
                copy(&self.emulator_log, &persistent_emu_log)?;
                println!("Emulator log is copied to {}", persistent_emu_log.display());
            }
            // device_launcher will return exit code:
            // Launcher = 1 <- default
            // AEMUCrash = 2
            // UserActionRequired = 3
            // SSHConnection = 4
            //
            // We only bail! if device_launcher failed with a launcher related error.
            let exit_code = status.code().unwrap_or_default();
            if exit_code == 1 {
                ffx_bail!("Cannot start Fuchsia Emulator.")
            }
            return Ok(exit_code);
        }

        if !vdl_args.tuntap {
            // When using SLIRP and running as ffx plugin (i.e ffx vdl start ...), automatically add device to ffx target
            if let Some(proxy) = daemon_proxy {
                println!("[fvdl] adding manual target at port: {} to ffx", ssh_port);
                target::add_target(proxy, ssh_port).await?;
            }
        }

        if !self.is_sdk {
            let command;
            if vdl_args.tuntap {
                command = String::from("fx set-device fuchsia-5254-0063-5e7a");
            } else {
                command = format!("fx set-device 127.0.0.1:{}", ssh_port);
            }
            println!(
                "{}",
                Yellow
                    .paint(format!("To support fx tools on emulator, please run \"{}\"", command))
            );
        }
        if start_command.nointeractive {
            println!(
                "{}",
                Yellow.paint(
                    "\nNOTE: For --nointeractive, launcher artifacts need to be manually cleaned using the `kill` subcommand:"));
            if !self.is_sdk {
                println!(
                    "{}",
                    Yellow.paint(format!(
                        "    ffx emu kill --launched-proto {}",
                        self.output_proto.display()
                    ))
                );
            } else {
                println!(
                    "{}",
                    Yellow.paint(format!(
                        "    fvdl --sdk kill --launched-proto {}",
                        self.output_proto.display()
                    ))
                );
            }
        } else {
            // TODO(fxbug.dev/72190) Ensure we have a way for user to interact with emulator
            // once SSH support goes away.
            let pid = get_emu_pid(&self.output_proto).unwrap();

            'keep_ssh: loop {
                match Command::new("pgrep").arg("qemu").output() {
                    Ok(out) => {
                        // pgrep can no longer find any qemu pid process we think the emulator has
                        // terminated, stop trying to ssh.
                        if out.stdout.is_empty() {
                            break 'keep_ssh;
                        }
                        if !str::from_utf8(&out.stdout)
                            .unwrap()
                            .lines()
                            .any(|p| p == pid.to_string())
                        {
                            break 'keep_ssh;
                        }
                        let ssh_out = self.ssh_and_wait(vdl_args.tuntap, ssh_port)?;
                        // If SSH process terminated successfully, we think user intend to end
                        // SSH session as well as shutting down emulator, stop trying to ssh.
                        // If SSH process terminated with a non-zero exit status, we think the
                        // user has issued "dm reboot", which reboots fuchsia and disconnects ssh,
                        // but emulator should still be running.
                        if ssh_out.status.success() {
                            break 'keep_ssh;
                        }
                    }
                    Err(_) => break 'keep_ssh,
                }
            }
            self.stop_vdl(
                &KillCommand {
                    launched_proto: Some(self.output_proto.display().to_string()),
                    vdl_path: Some(vdl.display().to_string()),
                },
                daemon_proxy,
            )
            .await?;
        }
        Ok(0)
    }

    /// SSH into the emulator and wait for exit signal.
    fn ssh_and_wait(&self, tuntap: bool, ssh_port: Port) -> Result<Output, std::io::Error> {
        // Ref to SSH flags: http://man.openbsd.org/ssh_config
        let mut ssh_options = vec![
            "-o",
            "StrictHostKeyChecking=no",
            "-o",
            "CheckHostIP=no",
            "-o",
            "UserKnownHostsFile=/dev/null",
            "-o",
            "ConnectTimeout=10",
            "-o",
            "ServerAliveInterval=1",
            "-o",
            "ServerAliveCountMax=5",
            "-o",
            "LogLevel=ERROR",
        ];
        if tuntap {
            let device_addr = Command::new(&self.host_tools.device_finder)
                .args(&["resolve", "-ipv4=false", "fuchsia-5254-0063-5e7a"])
                .output()?;
            ssh_options.append(&mut vec![
                "-i",
                &self.ssh_files.private_key.to_str().unwrap(),
                str::from_utf8(&device_addr.stdout).unwrap().trim_end_matches('\n'),
            ]);
            Command::new("ssh").args(&ssh_options).spawn()?.wait_with_output()
        } else {
            let port = &ssh_port.to_string();
            ssh_options.append(&mut vec![
                "-i",
                &self.ssh_files.private_key.to_str().unwrap(),
                "fuchsia@localhost",
                "-p",
                port,
            ]);
            Command::new("ssh").args(&ssh_options).spawn()?.wait_with_output()
        }
    }

    // Shuts down emulator and local services.
    pub async fn stop_vdl(
        &self,
        kill_command: &KillCommand,
        daemon_proxy: Option<&bridge::DaemonProxy>,
    ) -> Result<()> {
        let invoker = self.resolve_invoker();
        // If user specified vdl_path in arg, use that. If not, check if environment variable
        // PREBUILD_VDL_DIR is set, if set use that. If not, check if self.host_tools has found
        // default path for device_launcher, when running in-tree this will be read from
        // tool_paths.json, when running in sdk, this will be empty; if found, use that.
        // If empty (i.e from sdk), look for the tool in sdk_data_dir.
        let vdl: PathBuf = match &kill_command.vdl_path {
            Some(vdl_path) => PathBuf::from(vdl_path),
            None => match read_env_path("PREBUILT_VDL_DIR") {
                Ok(default_path) => default_path.join("device_launcher"),
                _ => {
                    if self.host_tools.vdl.as_os_str().is_empty() {
                        let label = self
                            .host_tools
                            .read_prebuild_version("device_launcher.version")
                            .unwrap_or(String::from("latest"));
                        get_sdk_data_dir()?
                            .join("femu")
                            .join(format!("vdl-{}", label.replace(":", "-")))
                            .join("device_launcher")
                    } else {
                        self.host_tools.vdl.clone()
                    }
                }
            },
        };
        if !vdl.exists() || !vdl.is_file() {
            ffx_bail!("device_launcher binary cannot be found at {}", vdl.display())
        }
        match &kill_command.launched_proto {
            None => {
                ffx_bail!(
                    "--launched-proto must be specified for `kill` subcommand.\n\
                    example: \"ffx emu kill --launched-proto /path/to/saved/output.log\"\n\
                    example: \"./fvdl --sdk kill --launched-proto /path/to/saved/output.log\"\n"
                )
            }
            Some(proto_location) => {
                Command::new(&vdl)
                    .arg("--action=kill")
                    .arg(format!("--launched_virtual_device_proto={}", &proto_location))
                    .arg(format!("--event_action={}", &invoker))
                    .status()?;
                if let Ok(ssh_port) = get_ssh_port(&PathBuf::from(proto_location)) {
                    if ssh_port != 0 && ssh_port != 22 {
                        if let Some(proxy) = daemon_proxy {
                            let mut target = format!("127.0.0.1:{}", ssh_port);
                            println!("[fvdl] removing manual target {} from ffx", target);
                            let result = target::remove_target(proxy, &mut target, &mut 3).await;
                            if result.is_err() {
                                println!("{}", Yellow.paint(format!(
                                        "\nNOTE: Target removal failed due to error {:?}.\n\
                                        To remove this target, please run 'ffx target remove 127.0.0.1:{}'", result.err(), ssh_port
                                    )));
                            }
                        }
                    }
                }
            }
        }
        Ok(())
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use serial_test::serial;

    pub fn setup() {
        env::set_var("HOST_OUT_DIR", "/host/out");
        env::set_var("FUCHSIA_BUILD_DIR", "/build/out");
    }

    pub fn create_start_command() -> StartCommand {
        StartCommand {
            tuntap: true,
            upscript: Some("/path/to/upscript".to_string()),
            packages_to_serve: Some("pkg1.far,pkg2.far".to_string()),
            aemu_path: Some("/path/to/aemu".to_string()),
            vdl_path: Some("/path/to/device_launcher".to_string()),
            host_gpu: true,
            grpcwebproxy_path: Some("/path/to/grpcwebproxy".to_string()),
            pointing_device: Some("mouse".to_string()),
            aemu_version: Some("git_revision:da1cc2ee512714a176f08b8b5fec035994ca305d".to_string()),
            grpcwebproxy_version: Some("git_revision:1".to_string()),
            sdk_version: Some("0.20201130.3.1".to_string()),
            image_name: Some("qemu-x64".to_string()),
            vdl_version: Some("git_revision:2".to_string()),
            envs: vec!["A=1".to_string(), "B=2".to_string(), "C=3".to_string()],
            fvm_image: Some("fvm".to_string()),
            zbi_image: Some("zircona".to_string()),
            kernel_image: Some("kernel".to_string()),
            ..Default::default()
        }
    }
    #[test]
    #[serial]
    fn test_choosing_prebuild_with_path_specified() -> Result<()> {
        setup();
        let start_command = &create_start_command();

        // --sdk
        let aemu = VDLFiles::new(true, false)?.resolve_aemu_path(start_command)?;
        assert_eq!(PathBuf::from("/path/to/aemu"), aemu);
        let vdl = VDLFiles::new(true, false)?.resolve_vdl_path(start_command)?;
        assert_eq!(PathBuf::from("/path/to/device_launcher"), vdl);
        let grpcwebproxy = VDLFiles::new(true, false)?.resolve_grpcwebproxy_path(start_command)?;
        assert_eq!(PathBuf::from("/path/to/grpcwebproxy"), grpcwebproxy);
        Ok(())
    }

    // TODO(fxb/73555) Mock download instead of downloading from cipd in this test.
    #[ignore]
    #[test]
    #[serial]
    fn test_choosing_prebuild_with_cipd_label_specified() -> Result<()> {
        setup();

        let tmp_dir = Builder::new().prefix("fvdl_test_cipd_label_").tempdir()?;
        env::set_var("FEMU_DOWNLOAD_DIR", tmp_dir.path());

        let mut start_command = &mut create_start_command();
        start_command.vdl_path = None;
        start_command.vdl_version = Some("g3-revision:vdl_fuchsia_20210113_RC00".to_string());

        // --sdk
        let vdl = VDLFiles::new(true, false)?.resolve_vdl_path(start_command)?;
        assert_eq!(
            tmp_dir.path().join("vdl-g3-revision-vdl_fuchsia_20210113_RC00/device_launcher"),
            vdl
        );
        Ok(())
    }

    // TODO(fxb/73555) Mock download instead of downloading from cipd in this test.
    #[ignore]
    #[test]
    #[serial]
    fn test_choosing_prebuild_default() -> Result<()> {
        setup();

        let tmp_dir = Builder::new().prefix("fvdl_test_default_").tempdir()?;
        env::set_var("FEMU_DOWNLOAD_DIR", tmp_dir.path());

        let mut start_command = &mut create_start_command();
        start_command.aemu_path = None;
        start_command.aemu_version = None;
        start_command.vdl_path = None;
        start_command.vdl_version = None;
        start_command.grpcwebproxy_path = None;
        start_command.grpcwebproxy_version = None;

        // --sdk
        let vdl = VDLFiles::new(true, false)?.resolve_vdl_path(start_command)?;
        assert_eq!(tmp_dir.path().join("vdl-latest/device_launcher"), vdl);
        let aemu = VDLFiles::new(true, false)?.resolve_aemu_path(start_command)?;
        assert_eq!(tmp_dir.path().join("aemu-integration/emulator"), aemu);
        let grpcwebproxy = VDLFiles::new(true, false)?.resolve_grpcwebproxy_path(start_command)?;
        assert_eq!(tmp_dir.path().join("grpcwebproxy-latest/grpcwebproxy"), grpcwebproxy);
        Ok(())
    }

    #[test]
    #[serial]
    fn test_resolve_portmap() -> Result<()> {
        setup();

        let mut start_command = &mut create_start_command();
        start_command.port_map = None;
        let (port_map, ssh) = VDLFiles::new(true, false)?.resolve_portmap(start_command);
        assert!(ssh > 0);
        let re = Regex::new(r"hostfwd=tcp::\d+-:22").unwrap();
        assert!(re.is_match(&port_map));

        start_command.port_map = Some("".to_string());
        let (port_map, ssh) = VDLFiles::new(true, false)?.resolve_portmap(start_command);
        assert!(ssh > 0);
        let re = Regex::new(r"hostfwd=tcp::\d+-:22").unwrap();
        assert!(re.is_match(&port_map));

        start_command.port_map = Some("hostfwd=tcp::123-:222,hostfwd=tcp::80-:223".to_string());
        let (port_map, ssh) = VDLFiles::new(true, false)?.resolve_portmap(start_command);
        assert!(ssh > 0);
        let re =
            Regex::new(r"hostfwd=tcp::123-:222,hostfwd=tcp::80-:223,hostfwd=tcp::\d+-:22").unwrap();
        assert!(re.is_match(&port_map));

        start_command.port_map =
            Some("hostfwd=tcp::123-:223,hostfwd=tcp::80-:322,hostfwd=tcp::456-:22".to_string());
        let (port_map, ssh) = VDLFiles::new(true, false)?.resolve_portmap(start_command);
        assert_eq!(456, ssh);
        assert_eq!("hostfwd=tcp::123-:223,hostfwd=tcp::80-:322,hostfwd=tcp::456-:22", port_map);

        start_command.port_map = Some("hostfwd=tcp::789-:22".to_string());
        let (port_map, ssh) = VDLFiles::new(true, false)?.resolve_portmap(start_command);
        assert_eq!(789, ssh);
        assert_eq!("hostfwd=tcp::789-:22", port_map);

        start_command.port_map =
            Some("hostfwd=tcp::123-:22,hostfwd=tcp::80-:8022,hostfwd=tcp::456-:222".to_string());
        let (port_map, ssh) = VDLFiles::new(true, false)?.resolve_portmap(start_command);
        assert_eq!(123, ssh);
        assert_eq!("hostfwd=tcp::123-:22,hostfwd=tcp::80-:8022,hostfwd=tcp::456-:222", port_map);
        Ok(())
    }

    #[test]
    #[serial]
    fn test_pid_check() -> Result<()> {
        let out = vec![51, 49, 51, 49, 54, 51, 55, 10, 51, 49, 51, 50, 50, 57, 51, 10];
        let pid_match = 3132293;
        let pid_no_match = 123;
        assert!(str::from_utf8(&out).unwrap().lines().any(|p| p == pid_match.to_string()));
        assert!(!str::from_utf8(&out).unwrap().lines().any(|p| p == pid_no_match.to_string()));
        Ok(())
    }

    #[test]
    #[serial]
    fn test_resolve_analytics_label() -> Result<()> {
        let mut label = VDLFiles::new(true /* is_sdk */, false /* verbose */)?.resolve_invoker();
        assert_eq!(label, "fvdl-sdk");
        env::set_var(ANALYTICS_ENV_VAR, "apple-pie");
        label = VDLFiles::new(true /* is_sdk */, false /* verbose */)?.resolve_invoker();
        assert_eq!(label, "apple-pie");
        Ok(())
    }
}
