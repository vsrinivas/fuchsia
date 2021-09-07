// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    argh::FromArgs,
    ffx_core::ffx_command,
    serde::{Deserialize, Serialize},
};

/// entry point for ffx
#[ffx_command()]
#[derive(FromArgs, Debug, PartialEq)]
#[argh(subcommand, name = "emu", description = "Start and manage Fuchsia emulators")]
pub struct EmulatorCommand {
    #[argh(subcommand)]
    pub command: VDLCommand,
    /// running in fuchsia sdk (not inside the fuchsia code repository)
    #[argh(switch)]
    pub sdk: bool,
}

/// entry point for fvdl
#[derive(FromArgs, Debug, PartialEq)]
/// Commands to start/stop the emulator via fuchsia virtual device launcher (VDL)
pub struct Args {
    #[argh(subcommand)]
    pub command: VDLCommand,
    /// running in fuchsia sdk (not inside the fuchsia code repository)
    #[argh(switch)]
    pub sdk: bool,
}

#[derive(FromArgs, Debug, PartialEq)]
#[argh(subcommand)]
pub enum VDLCommand {
    Start(StartCommand),
    Kill(KillCommand),
    Remote(RemoteCommand),
}

#[derive(FromArgs, Default, Debug, PartialEq, Deserialize, Serialize)]
#[argh(subcommand, name = "start")]
/// Starting Fuchsia Emulator
pub struct StartCommand {
    /// bool, run emulator in headless mode.
    #[argh(switch, short = 'H')]
    pub headless: bool,

    /// bool, run emulator with emulated nic via tun/tap.
    #[argh(switch, short = 'N')]
    pub tuntap: bool,

    /// bool, run emulator with host GPU acceleration, this doesn't work on remote-desktop with --headless.
    #[argh(switch)]
    pub host_gpu: bool,

    /// bool, run emulator without host GPU acceleration, default.
    #[argh(switch)]
    pub software_gpu: bool,

    /// bool, enable pixel scaling on HiDPI devices.
    #[argh(switch)]
    pub hidpi_scaling: bool,

    /// path to tun/tap upscript, this script will be executed before booting up FEMU.
    #[argh(option, short = 'u')]
    pub upscript: Option<String>,

    /// set pointing device used on emulator: mouse or touch screen. Allowed values are "touch", "mouse". Default is "touch".
    #[argh(option, short = 'p')]
    pub pointing_device: Option<String>,

    /// emulator window width. Default to 1280.
    #[argh(option, default = "default_window_width()", short = 'w')]
    pub window_width: usize,

    /// emulator window height. Default to 800.
    #[argh(option, default = "default_window_height()", short = 'h')]
    pub window_height: usize,

    /// extends storage size to <size> bytes. Default is "2G".
    #[argh(option, short = 's')]
    pub image_size: Option<String>,

    /// path to fuchsia virtual device configuration, if not specified a generic one will be generated.
    #[argh(option, short = 'F')]
    pub device_proto: Option<String>,

    /// path to aemu location.
    /// When running in fuchsia repo, defaults to looking in prebuilt/third_party/aemu/PLATFORM.
    /// When running in fuchsia sdk, defaults to looking in $HOME/.fuchsia/femu.
    #[argh(option, short = 'e')]
    pub aemu_path: Option<String>,

    /// label used to download AEMU from CIPD. Default is "integration".
    /// Download only happens if aemu binary cannot be found from known paths.
    #[argh(option)]
    pub aemu_version: Option<String>,

    /// device_launcher binary location.
    /// When running in fuchsia repo, defaults to looking in prebuilt/vdl/device_launcher.
    /// When running in fuchsia sdk, defaults to looking in directory containing `fvdl`.
    #[argh(option, short = 'd')]
    pub vdl_path: Option<String>,

    /// label used to download vdl from CIPD. Default is "latest".
    /// Download only happens if vdl (device_launcher) binary cannot be found from known paths.
    #[argh(option)]
    pub vdl_version: Option<String>,

    /// enable WebRTC HTTP service on port, if set to 0 a random port will be picked
    #[argh(option, short = 'x')]
    pub grpcwebproxy: Option<usize>,

    /// location of grpcwebproxy,
    /// When running in fuchsia repo, defaults to looking in prebuilt/third_party/grpcwebproxy
    /// When running in fuchsia sdk, defaults to looking in $HOME/.fuchsia/femu.
    #[argh(option, short = 'X')]
    pub grpcwebproxy_path: Option<String>,

    /// label used to download grpcwebproxy from CIPD. Default is "latest".
    /// Download only happens if --grpcwebproxy is set and grpcwebproxy binary cannot be found from known paths or path specified by --grpcwebproxy_path.
    #[argh(option)]
    pub grpcwebproxy_version: Option<String>,

    /// fuchsia sdk ID used to fetch from gcs, if specified, the emulator will launch with fuchsia sdk files fetched from gcs.
    /// To find the latest version run `gsutil cat gs://fuchsia/development/LATEST_LINUX`.
    #[argh(option, short = 'v')]
    pub sdk_version: Option<String>,

    /// gcs bucket name. Default is "fuchsia".
    #[argh(option)]
    pub gcs_bucket: Option<String>,

    /// image file name used to fetch from gcs. Default is "qemu-x64".
    /// To view available image names run `gsutil ls -l gs://fuchsia/development/$(gsutil cat gs://fuchsia/development/LATEST_LINUX)/images`.
    #[argh(option)]
    pub image_name: Option<String>,

    /// file path to store emulator log. Default is a temp file that is deleted after `fvdl` exits.
    #[argh(option, short = 'l')]
    pub emulator_log: Option<String>,

    /// host port mapping for user-networking mode. This flag will be ignored if --tuntap is used.
    /// If not specified, an ssh port on host will be randomly picked and forwarded.
    /// ex: hostfwd=tcp::<host_port>-:<guest_port>,hostfwd=tcp::<host_port>-:<guest_port>
    #[argh(option)]
    pub port_map: Option<String>,

    /// file destination to write `device_launcher` output.
    /// Required for --nointeractive mode. Default is a temp file that is deleted after `fvdl` exits.
    /// Specify this flag if you plan to use the `kill` subcommand.
    #[argh(option)]
    pub vdl_output: Option<String>,

    /// extra kernel flags to pass into aemu.
    #[argh(option, short = 'c')]
    pub kernel_args: Option<String>,

    /// bool, turn off interactive mode.
    /// if turned off, fvdl will not land user in ssh console. A ssh port will still be forwarded.
    /// User needs to specify --vdl-output flag with this mode, and manually call
    /// the `kill` subcommand to perform clean shutdown.
    #[argh(switch)]
    pub nointeractive: bool,

    /// bool, download and re-use image files in the cached location ~/.fuchsia/<image_name>/<sdk_version>/.
    /// If not set (default), image files will be stored in a temp location and removed with `kill` subcommand.
    /// If image location is specified with --kernel-image, --zbi-image, --fvm-image etc., the cached image will
    /// be overwritten for the specified image file.
    #[argh(switch, short = 'i')]
    pub cache_image: bool,

    /// bool, pause on launch and wait for a debugger process to attach before resuming
    #[argh(switch)]
    pub debugger: bool,

    /// bool, launches emulator in qemu console
    /// No local services such as package_server will be running in this mode.
    #[argh(switch, short = 'm')]
    pub monitor: bool,

    /// bool, launches user in femu serial console, this flag is required for bringup image.
    /// No local services such as package_server will be running in this mode.
    #[argh(switch)]
    pub emu_only: bool,

    /// deprecated, does nothing, will soon be removed.
    #[argh(switch)]
    pub nopackageserver: bool,

    /// bool, enables automatically launching package server.
    #[argh(switch, short = 'P')]
    pub start_package_server: bool,

    /// comma separated string of fuchsia package urls, extra packages to serve after starting FEMU.
    /// Requires --start_package_server=true
    #[argh(option)]
    pub packages_to_serve: Option<String>,

    /// file path to store package server log. Default is a stdout.
    /// Requires --start_package_server=true
    #[argh(option)]
    pub package_server_log: Option<String>,

    /// path to unpack archived_package downloaded from GCS. This only applies when fvdl is
    /// downloading images files from GCS (ex: --gcs-bucket, --sdk-verion, --image-name flags
    /// are specified). If not specified, a temporary path will be used.
    #[argh(option)]
    pub amber_unpack_root: Option<String>,

    /// environment variables for emulator. The argument can be repeated for multiple times
    /// to add multiple arguments. If not specified, only the default environment variables
    /// (DISPLAY) will be set to run the emulator.
    #[argh(option)]
    pub envs: Vec<String>,

    /// bool, disable acceleration using KVM on Linux and HVF on macOS.
    #[argh(switch)]
    pub noacceleration: bool,

    /// int, port to an existing package server running on the host.
    #[argh(option)]
    pub package_server_port: Option<usize>,

    /// string, absolute path to amber-files location, path name must end with 'amber-files'.
    #[argh(option, short = 'a')]
    pub amber_files: Option<String>,

    /// string, absolute path to fvm image file location.
    #[argh(option, short = 'f')]
    pub fvm_image: Option<String>,

    /// string, absolute path to kernel image file location.
    /// If specified --zbi-image and --image-architecture must also be specified.
    /// When running with --sdk option, this will skip downloading fuchsia image prebuilts from GCS.
    #[argh(option, short = 'k')]
    pub kernel_image: Option<String>,

    /// string, absolute path to zircon image file location.
    /// If specified --kernel-image and --image-architecture must also be specified.
    /// When running with --sdk option, this will skip downloading fuchsia image prebuilts from GCS.
    #[argh(option, short = 'z')]
    pub zbi_image: Option<String>,

    /// string, specifies image architecture, accepted values are 'arm64' or 'x64'.
    /// Required if image override flags (i.e --fvm-image, --kernel-image, --zbi-image, or --amber-files)
    /// are specified.
    #[argh(option, short = 'A')]
    pub image_architecture: Option<String>,

    /// string, specifies an alternative path for ssh keys. The emulator defaults to the user's
    /// $HOME/.ssh directory if none is specified. The path indicated must contain the files
    /// `fuchsia_authorized_keys` and `fuchsia_ed25519`.
    #[argh(option)]
    pub ssh: Option<String>,

    /// bool, enables extra logging for debugging
    #[argh(switch, short = 'V')]
    pub verbose: bool,

    /// bool, terminates the plugin before it calls out to the next layer, and prints the command
    /// to the screen for debugging. The temporary staging directory is also retained.
    #[argh(switch)]
    pub dry_run: bool,
}

fn default_window_height() -> usize {
    800
}

fn default_window_width() -> usize {
    1280
}

fn default_port() -> u16 {
    8080
}

fn default_display() -> String {
    "xvfb-run".to_string()
}

fn default_dir() -> String {
    "~/fuchsia".to_string()
}

#[derive(FromArgs, Debug, PartialEq)]
#[argh(subcommand, name = "kill")]
/// Killing Fuchsia Emulator
pub struct KillCommand {
    /// device_launcher binary location. Defaults to looking in prebuilt/vdl/device_launcher
    #[argh(option, short = 'd')]
    pub vdl_path: Option<String>,
    /// required, file containing device_launcher process artifact location.
    #[argh(option)]
    pub launched_proto: Option<String>,
}

#[derive(FromArgs, Default, Debug, PartialEq)]
#[argh(subcommand, name = "remote")]
/// This is a placeholder for a new feature in active development. Please stand by...
// Connect to <host>, run a build using fx from <dir>, fetch the artifacts and
// start the emulator. Alternatively, start the emulator on <host>,
// and open an WebRTC connection to it using local browser.
pub struct RemoteCommand {
    /// the hostname to connect to
    #[argh(positional)]
    pub host: String,

    /// defaults to ~/fuchsia, the path to the FUCHSIA_DIR on <host>
    #[argh(option, default = "default_dir()")]
    pub dir: String,

    /// do not build, just pull artifacts already present
    #[argh(switch)]
    pub no_build: bool,

    /// stream output from remote emulator using WebRTC instead of fetching artifacts
    #[argh(switch)]
    pub stream: bool,

    /// only tunnel, do not start remote emulator
    #[argh(switch)]
    pub no_emu: bool,

    /// do not use turn configuration for remote emulator
    #[argh(switch)]
    pub no_turn: bool,

    /// do not open https://web-femu.appspot.com, just run remote emulator
    #[argh(switch)]
    pub no_open: bool,

    /// do not start remote virtual display, use DPY instead
    #[argh(option, default = "default_display()")]
    pub display: String,

    /// port used on local machine to connect with remote emulator over HTTP (default: 8080)
    #[argh(option, default = "default_port()")]
    pub port: u16,

    /// arguments to pass to the emulator
    #[argh(positional)]
    pub args: Vec<String>,
}
