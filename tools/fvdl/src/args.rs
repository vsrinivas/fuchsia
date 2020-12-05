// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use argh::FromArgs;

#[derive(FromArgs, Debug, PartialEq)]
/// Commands to start/stop the emulator via fuchsia virtual device launcher (VDL)
pub struct Args {
    #[argh(subcommand)]
    pub command: VDLCommand,
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
    /// run in headless mode.
    #[argh(switch, short = 'H')]
    pub headless: bool,

    /// run with emulated nic via tun/tap.
    #[argh(switch, short = 'N')]
    pub tuntap: bool,

    /// run with host GPU acceleration, this doesn't work on remote-desktop && headless=false.
    #[argh(switch)]
    pub host_gpu: bool,

    /// run without host GPU acceleration, default.
    #[argh(switch)]
    pub software_gpu: bool,

    /// path to emu upscript, this script will be executed before booting up femu.
    #[argh(option, short = 'u')]
    pub upscript: Option<String>,

    /// extra packages to serve after starting FEMU, this is a comma separated string of fuchsia package urls.
    #[argh(option)]
    pub packages_to_serve: Option<String>,

    /// set pointing device used on emulator: mouse or touch screen. Allowed values are "touch", "mouse", default is "touch".
    #[argh(option, short = 'p')]
    pub pointing_device: Option<String>,

    /// extends the fvm image size to <size> bytes. Default is "2G"
    #[argh(option, short = 's')]
    pub image_size: Option<String>,

    /// path to fuchsia virtual device configuration, if not specified a generic one will be generated.
    #[argh(option, short = 'f')]
    pub device_proto: Option<String>,

    /// path to aemu location, defaults to looking in prebuilt/third_party/aemu/PLATFORM
    #[argh(option, short = 'e')]
    pub aemu_path: Option<String>,

    /// device_launcher/vdl binary location. Defaults to looking in prebuilt/vdl/device_launcher
    #[argh(option, short = 'd')]
    pub vdl_path: Option<String>,

    /// location of grpcwebproxy, defaults to looking in prebuilt/third_party/grpcwebproxy
    #[argh(option, short = 'X')]
    pub grpcwebproxy_path: Option<String>,

    /// enable WebRTC HTTP service on port, if not set to 0 a random port will be picked
    #[argh(option, short = 'x')]
    pub grpcwebproxy: Option<usize>,

    /// emulator window width, default to 1280
    #[argh(option, default = "default_window_width()", short = 'w')]
    pub window_width: usize,

    /// emulator window height, default to 800
    #[argh(option, default = "default_window_height()", short = 'h')]
    pub window_height: usize,
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
    /// file containing vdl process artifact location.
    #[argh(option)]
    pub launched_proto: Option<String>,

    /// device_launcher/vdl binary location. Defaults to looking in prebuilt/vdl/device_launcher
    #[argh(option, short = 'd')]
    pub vdl_path: Option<String>,
}
