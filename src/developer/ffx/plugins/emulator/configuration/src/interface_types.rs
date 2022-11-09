// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Common data types for emulator engines. These are implementation-agnostic
//! data types, not the engine-specific command types that each engine will define for itself. These
//! types will be directly deserializable from the PBM, and converted into engine-specific types at
//! runtime.

use crate::enumerations::{
    AccelerationMode, ConsoleType, EngineConsoleType, EngineType, GpuType, LogLevel,
    NetworkingMode, OperatingSystem, PortMapping, ShowDetail, VirtualCpu,
};
use anyhow::Result;
use async_trait::async_trait;
use fidl_fuchsia_developer_ffx as ffx;
use sdk_metadata::{AudioDevice, CpuArchitecture, DataAmount, PointingDevice, Screen};
use serde::{Deserialize, Serialize};
use std::{collections::HashMap, path::PathBuf, process::Command, time::Duration};

#[async_trait]
pub trait EmulatorEngine {
    /// Prepare an emulator to run. This function shouldn't require any additional configuration as
    /// input, since the object should be fully configured by the EngineBuilder. At its most basic,
    /// this should assemble the command-line to invoke the emulator binary. If support processes
    /// are required, or temporary files need to be written to disk, that would be handled here.
    async fn stage(&mut self) -> Result<()>;

    /// Given a staged emulator instance, start it running. When the function returns, either the
    /// emulator will be running independently, or an error will be sent back explaining the failure.
    async fn start(
        &mut self,
        mut emulator_cmd: Command,
        proxy: &ffx::TargetCollectionProxy,
    ) -> Result<i32>;

    /// Shut down a running emulator instance. The engine should have been instantiated from a saved
    /// and serialized instance, so no additional initialization should be needed. This function
    /// will terminate a running emulator instance, which will be specified on the command line. It
    /// may return an error if the instance doesn't exist or the shut down fails, but should succeed
    /// if it's no longer running or gets successfully shut down.
    async fn stop(&self, proxy: &ffx::TargetCollectionProxy) -> Result<()>;

    /// Output the details of an existing emulation instance. The engine should have been
    /// instantiated from a saved and serialized instance, so no additional initialization should be
    /// needed. This function will output text to the terminal describing the instance, its status,
    /// and its configuration. This is an engine-specific output with more detail than `ffx list`.
    fn show(&self, details: Vec<ShowDetail>);

    /// Validate the configuration parameters that have been provided to this engine, according to
    /// the requirements for this engine type. If there are fields which are required, mutually
    /// exclusive, only work when applied in certain combinations, or don't apply to this engine
    /// type, this function will return an error indicating which field(s) and why. It also returns
    /// an error if the engine has already been started. Otherwise, this returns Ok(()).
    fn validate(&self) -> Result<()>;

    /// Returns the EngineType used when building this engine. Each engine implementation should
    /// always return the same EngineType.
    fn engine_type(&self) -> EngineType;

    /// Returns true if this instance of the emulator is currently running.
    /// This is checked by using signal to the process id, no consideration is
    /// made for multi threaded access.
    fn is_running(&self) -> bool;

    /// Once the engine has been staged, this generates the command line required to start
    /// emulation. There are no side-effects, and the operation can be repeated as necessary.
    fn build_emulator_cmd(&self) -> Command;

    /// Access to the engine's emulator_configuration field.
    fn emu_config(&self) -> &EmulatorConfiguration;

    /// Mutable access to the engine's emulator_configuration field.
    fn emu_config_mut(&mut self) -> &mut EmulatorConfiguration;

    /// Attach the current process to one of the emulator's consoles.
    fn attach(&self, console: EngineConsoleType) -> Result<()>;
}

/// Collects the specific configurations into a single struct for ease of passing around.
#[derive(Clone, Debug, Default, Deserialize, PartialEq, Serialize)]
pub struct EmulatorConfiguration {
    pub device: DeviceConfig,
    pub flags: FlagData,
    pub guest: GuestConfig,
    pub host: HostConfig,
    pub runtime: RuntimeConfig,
}

#[derive(Clone, Debug, Default, Deserialize, PartialEq, Serialize)]
pub struct FlagData {
    /// Arguments. The set of flags which follow the "-fuchsia" option. These are not processed by
    /// Femu, but are passed through to Qemu.
    pub args: Vec<String>,

    /// Environment Variables. These are not passed on the command line, but are set in the
    /// process's environment before execution.
    pub envs: HashMap<String, String>,

    /// Features. A Femu-only field. Features are the first set of command line flags passed to the
    /// Femu binary. These are single words, capitalized, comma-separated, and immediately follow
    /// the flag "-feature".
    pub features: Vec<String>,

    /// Kernel Arguments. The last part of the command line. A set of text values that are passed
    /// through the emulator executable directly to the guest system's kernel.
    pub kernel_args: Vec<String>,

    /// Options. A Femu-only field. Options come immediately after features. Options may be boolean
    /// flags (e.g. -no-hidpi-scaling) or have associated values (e.g. -window-size 1280x800).
    pub options: Vec<String>,
}

/// Specifications of the virtual device to be emulated.
#[derive(Clone, Debug, Default, Deserialize, PartialEq, Serialize)]
pub struct DeviceConfig {
    /// The model of audio device being emulated, if any.
    pub audio: AudioDevice,

    /// The architecture and number of CPUs to emulate on the guest system.
    pub cpu: VirtualCpu,

    /// The amount of virtual memory to emulate on the guest system.
    pub memory: DataAmount,

    /// Which input source to emulate for screen interactions on the guest, if any.
    pub pointing_device: PointingDevice,

    /// The dimensions of the virtual device's screen, if any.
    pub screen: Screen,

    /// The amount of virtual storage to allocate to the guest's storage device, which will be
    /// populated by the GuestConfig's fvm_image. Only one virtual storage device is supported
    /// at this time.
    pub storage: DataAmount,
}

/// Image files and other information specific to the guest OS.
#[derive(Clone, Debug, Default, Deserialize, PartialEq, Serialize)]
pub struct GuestConfig {
    /// Fuchsia Volume Manager image, this is the guest's virtual storage device.
    pub fvm_image: Option<PathBuf>,

    /// The Fuchsia kernel, which loads alongside the ZBI and brings up the OS.
    pub kernel_image: PathBuf,

    /// Zircon Boot image, this is Fuchsia's initial ram disk used in the boot process.
    pub zbi_image: PathBuf,
}

/// Host-side configuration data, such as physical hardware and host OS details.
#[derive(Clone, Debug, Default, Deserialize, PartialEq, Serialize)]
pub struct HostConfig {
    /// Determines the type of hardware acceleration to use for emulation, such as KVM.
    pub acceleration: AccelerationMode,

    /// Indicates the CPU architecture of the host system.
    pub architecture: CpuArchitecture,

    /// Determines the type of graphics acceleration, to improve rendering in the guest OS.
    pub gpu: GpuType,

    /// Local IP address used to locate user mode networking emulators.
    #[serde(default)]
    pub local_ip_addr: String,

    /// Specifies the path to the emulator's log files.
    pub log: PathBuf,

    /// Determines the networking type for the emulator.
    pub networking: NetworkingMode,

    /// Indicates the operating system the host system is running.
    pub os: OperatingSystem,

    /// Holds a set of named ports, with the mapping from host to guest for each one.
    /// Generally only useful when networking is set to "user".
    pub port_map: HashMap<String, PortMapping>,
}

/// A collection of properties which control/influence the
/// execution of an emulator instance. These are different from the
/// DeviceConfig and GuestConfig which defines the hardware configuration
/// and behavior of Fuchsia running within the emulator instance.
#[derive(Clone, Debug, Default, Deserialize, PartialEq, Serialize)]
pub struct RuntimeConfig {
    /// The emulator's output, which might come from the serial console, the guest, or nothing.
    pub console: ConsoleType,

    /// Pause the emulator and wait for the user to attach a debugger to the process.
    pub debugger: bool,

    /// Run the emulator without a GUI. Graphics drivers will still be loaded.
    pub headless: bool,

    /// On machines with high-density screens (such as MacBook Pro), window size may be
    /// scaled to match the host's resolution which results in a much smaller GUI.
    pub hidpi_scaling: bool,

    /// The staging and working directory for the emulator instance.
    pub instance_directory: PathBuf,

    /// Additional arguments to pass directly to the emulator.
    #[serde(default)]
    pub addl_kernel_args: Vec<String>,

    /// The verbosity level of the logs for this instance.
    pub log_level: LogLevel,

    // A generated MAC address for the emulators virtual network.
    pub mac_address: String,

    /// The human-readable name for this instance. Must be unique from any other current
    /// instance on the host.
    pub name: String,

    /// Whether or not the emulator should reuse a previous instance's image files.
    #[serde(default)]
    pub reuse: bool,

    /// Maximum amount of time to wait on the emulator health check to succeed before returning
    /// control to the user.
    pub startup_timeout: Duration,

    /// Path to an enumeration flags template file, which contains a Handlebars-renderable
    /// set of arguments to be passed to the Command which starts the emulator.
    pub template: PathBuf,

    /// Optional path to a Tap upscript file, which is passed to the emulator when Tap networking
    /// is enabled.
    pub upscript: Option<PathBuf>,

    /// Engine type name. Added here to be accessible in the configuration template processing.
    #[serde(default)]
    pub engine_type: EngineType,
}
