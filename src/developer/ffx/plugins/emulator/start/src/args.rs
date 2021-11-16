// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use argh::{FromArgValue, FromArgs};
use ffx_core::ffx_command;
use ffx_emulator_config::{AccelerationMode, GpuType};
use serde::{Deserialize, Serialize};
use std::path::PathBuf;

#[derive(Clone, Copy, Debug, Deserialize, PartialEq, Serialize)]
#[serde(rename_all = "lowercase")]
pub enum EngineType {
    /// Fuchsia Emulator based on AEMU. Supports graphics.
    Femu,

    /// Qemu emulator. Version 5.
    Qemu,
}

impl FromArgValue for EngineType {
    fn from_arg_value(text: &str) -> Result<Self, std::string::String> {
        let value = serde_json::from_str(&format!("\"{}\"", text)).expect(&format!(
            "could not parse '{}' as a valid EngineType. \
            Please check the help text for allowed values and try again",
            text
        ));
        Ok(value)
    }
}

impl Default for EngineType {
    fn default() -> Self {
        EngineType::Femu
    }
}

#[ffx_command()]
#[derive(Clone, FromArgs, Debug, Default, PartialEq)]
#[argh(subcommand, name = "start")]
/// Starting Fuchsia Emulator
pub struct StartCommand {
    /// name of emulator instance. This is used to identify the instance.
    /// Default is 'fuchsia-emulator'.
    #[argh(option, default = "\"fuchsia-emulator\".to_string()")]
    pub name: String,

    /// emulator engine to use for this instance.  Allowed values are
    ///  "femu", "qemu". Default is "femu".
    #[argh(option, default = "EngineType::Femu")]
    pub engine: EngineType,

    /// run emulator in headless mode where there is no GUI.
    #[argh(switch, short = 'H')]
    pub headless: bool,

    /// run emulator with network in bridge mode via tun/tap.
    /// This option is not supported on MacOS.
    /// TODO(fxbug.dev/88327): Support SLIRP and No network.
    #[argh(switch, short = 'N')]
    pub tuntap: bool,

    /// configure GPU acceleration to run the emulator. Allowed values are "host", "guest",
    /// "swiftshader_indirect", or "auto". Default is "auto". Note: This only affects
    /// FEMU emulator.
    #[argh(option)]
    pub gpu: Option<GpuType>,

    /// virtualization acceleration. Valid choices are "none" to disable acceleration, "hyper" to use the
    /// host's hypervisor interface, KVM on Linux and HVF on macOS, "auto" to use the hypervisor if detected.
    /// The default value is "auto".
    #[argh(option, default = "AccelerationMode::Auto")]
    pub accel: AccelerationMode,

    /// enable pixel scaling on HiDPI devices (MacOS).
    #[argh(switch)]
    pub hidpi_scaling: bool,

    /// file path to store emulator log.
    #[argh(option, short = 'l')]
    pub log: Option<PathBuf>,

    /// host port mapping for user-networking mode.
    /// TODO(fxbug.dev/88327): enable SLIRP.
    #[argh(option)]
    pub port_map: Option<String>,

    /// pause on launch and wait for a debugger process to attach before resuming.
    #[argh(switch)]
    pub debugger: bool,

    /// launches emulator in qemu console.
    #[argh(switch, short = 'm')]
    pub monitor: bool,

    /// launches user in femu serial console.
    #[argh(switch)]
    pub console: bool,

    /// environment variables for emulator. The argument can be repeated for multiple times
    /// to add multiple arguments. If not specified, only the environment variable
    /// (DISPLAY) will be set to run the emulator.
    #[argh(option)]
    pub envs: Vec<String>,

    /// use named product information from Product Bundle Metadata (PBM). If no
    /// product bundle is specified and there is an obvious choice, that will be
    /// used (e.g. if there is only one PBM available).
    #[argh(positional)]
    pub product_bundle: Option<String>,

    /// enables extra logging for debugging
    #[argh(switch, short = 'V')]
    pub verbose: bool,

    /// terminates the plugin before executing the emulator command.
    #[argh(switch)]
    pub dry_run: bool,
}
