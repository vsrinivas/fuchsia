// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use argh::FromArgs;

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
}

#[derive(FromArgs, Debug, PartialEq)]
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

    /// comma separated string of fuchsia package urls, extra packages to serve after starting FEMU.
    #[argh(option)]
    pub packages_to_serve: Option<String>,

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
    #[argh(option, short = 'f')]
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
    /// To view availabe image names run `gsutil ls -l gs://fuchsia/development/$(gsutil cat gs://fuchsia/development/LATEST_LINUX)/images`.
    #[argh(option)]
    pub image_name: Option<String>,
}

fn default_window_height() -> usize {
    800
}

fn default_window_width() -> usize {
    1280
}

#[derive(FromArgs, Debug, PartialEq)]
#[argh(subcommand, name = "kill")]
/// Killing Fuchsia Emulator
pub struct KillCommand {
    /// device_launcher binary location. Defaults to looking in prebuilt/vdl/device_launcher
    #[argh(option, short = 'd')]
    pub vdl_path: Option<String>,
    /// file containing device_launcher process artifact location.
    #[argh(option)]
    pub launched_proto: Option<String>,
}
