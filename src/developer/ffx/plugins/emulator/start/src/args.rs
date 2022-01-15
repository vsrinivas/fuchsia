// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use argh::FromArgs;
use ffx_core::ffx_command;
use ffx_emulator_config::{AccelerationMode, EngineType, GpuType, NetworkingMode};
use std::path::PathBuf;

#[ffx_command()]
#[derive(Clone, FromArgs, Debug, Default, PartialEq)]
#[argh(subcommand, name = "start")]
/// Starting Fuchsia Emulator
pub struct StartCommand {
    /// virtualization acceleration. Valid choices are "none" to disable acceleration, "hyper" to
    /// use the host's hypervisor interface, KVM on Linux and HVF on MacOS, "auto" to use the
    /// hypervisor if detected. The default value is "auto".
    #[argh(option, default = "AccelerationMode::Auto")]
    pub accel: AccelerationMode,

    /// launches user in femu serial console.
    #[argh(switch)]
    pub console: bool,

    /// pause on launch and wait for a debugger process to attach before resuming.
    #[argh(switch)]
    pub debugger: bool,

    /// terminates the plugin before executing the emulator command.
    #[argh(switch)]
    pub dry_run: bool,

    /// emulator engine to use for this instance.  Allowed values are
    ///  "femu", "qemu". Default is "femu".
    #[argh(option, default = "EngineType::Femu")]
    pub engine: EngineType,

    /// environment variables for emulator. The argument can be repeated for multiple times
    /// to add multiple arguments. If not specified, only the environment variable
    /// (DISPLAY) will be set to run the emulator.
    #[argh(option)]
    pub envs: Vec<String>,

    /// configure GPU acceleration to run the emulator. Allowed values are "host", "guest",
    /// "swiftshader_indirect", or "auto". Default is "auto". Note: This only affects
    /// FEMU emulator.
    #[argh(option, default = "GpuType::Auto")]
    pub gpu: GpuType,

    /// run emulator in headless mode where there is no GUI.
    #[argh(switch, short = 'H')]
    pub headless: bool,

    /// enable pixel scaling on HiDPI devices. Defaults to true for MacOS, false otherwise.
    #[argh(option, default = "std::env::consts::OS == \"macos\"")]
    pub hidpi_scaling: bool,

    /// file path to store emulator log.
    #[argh(option, short = 'l')]
    pub log: Option<PathBuf>,

    /// launches emulator in qemu console.
    #[argh(switch, short = 'm')]
    pub monitor: bool,

    /// name of emulator instance. This is used to identify the instance.
    /// Default is 'fuchsia-emulator'.
    #[argh(option, default = "\"fuchsia-emulator\".to_string()")]
    pub name: String,

    /// host port mapping for user-networking mode.
    /// Syntax is "--port-map <portname>:<port>".
    /// This flag may be repeated for multiple port mappings.
    #[argh(option)]
    pub port_map: Vec<String>,

    /// use named product information from Product Bundle Metadata (PBM). If no
    /// product bundle is specified and there is an obvious choice, that will be
    /// used (e.g. if there is only one PBM available).
    #[argh(positional)]
    pub product_bundle: Option<String>,

    /// specify a template file to populate the command line flags for the emulator.
    /// Defaults to a Handlebars template specified in the Product Bundle manifest.
    /// TODO(fxbug.dev/90948): Make this non-optional once the template file is
    /// included in the SDK.
    #[argh(option)]
    pub start_up_args_template: Option<PathBuf>,

    /// specify the networking type for the emulator: 'none', 'auto', 'tap' mode with Tun/Tap,
    /// or 'user' mode with SLiRP.
    #[argh(option, default = "NetworkingMode::Auto")]
    pub net: NetworkingMode,

    /// enables extra logging for debugging
    #[argh(switch, short = 'V')]
    pub verbose: bool,
}
