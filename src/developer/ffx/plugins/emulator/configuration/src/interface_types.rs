// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Common data types for emulator engines. These are implementation-agnostic
//! data types, not the engine-specific command types that each engine will define for itself. These
//! types will be directly deserializable from the PBM, and converted into engine-specific types at
//! runtime.

use crate::enumerations::{
    AccelerationMode, ConsoleType, EngineType, FilterResult, GpuType, LogLevel, NetworkingMode,
    VirtualCpu,
};
use anyhow::Result;
use async_trait::async_trait;
use sdk_metadata::{AudioDevice, Behavior, DataAmount, PointingDevice, Screen};
use serde::{Deserialize, Serialize};
use std::{collections::HashMap, path::PathBuf};

#[async_trait]
pub trait EmulatorEngine {
    /// Start the emulator running. This function shouldn't require any additional configuration as
    /// input, since the object should be fully configured by the EngineBuilder. At its most basic,
    /// this should assemble the command-line to invoke the emulator binary, then spawn the process
    /// and return. If support processes are required, or temporary files need to be written to
    /// disk, that would be handled here. When the function returns, either the emulator will be
    /// running independently, or an error will be sent back explaining the failure.
    async fn start(&mut self) -> Result<i32>;

    /// Shut down a running emulator instance. The engine should have been instantiated from a saved
    /// and serialized instance, so no additional initialization should be needed. This function
    /// will terminate a running emulator instance, which will be specified on the command line. It
    /// may return an error if the instance doesn't exist or the shut down fails, but should succeed
    /// if it's no longer running or gets successfully shut down.
    fn shutdown(&mut self) -> Result<()>;

    /// Output the details of an existing emulation instance. The engine should have been
    /// instantiated from a saved and serialized instance, so no additional initialization should be
    /// needed. This function will output text to the terminal describing the instance, its status,
    /// and its configuration. This is an engine-specific output with more detail than `ffx list`.
    fn show(&self);

    /// Validate the configuration parameters that have been provided to this engine, according to
    /// the requirements for this engine type. If there are fields which are required, mutually
    /// exclusive, only work when applied in certain combinations, or don't apply to this engine
    /// type, this function will return an error indicating which field(s) and why. It also returns
    /// an error if the engine has already been started. Otherwise, this returns Ok(()).
    fn validate(&self) -> Result<()>;

    /// Returns the EngineType used when building this engine. Each engine implementation should
    /// always return the same EngineType.
    fn engine_type(&self) -> EngineType;
}

/// Collects the specific configurations into a single struct for ease of passing around.
#[derive(Clone, Debug, Default, Deserialize, PartialEq, Serialize)]
pub struct EmulatorConfiguration {
    pub behavior: HashMap<String, Behavior>,
    pub device: DeviceConfig,
    pub guest: GuestConfig,
    pub host: HostConfig,
    pub runtime: RuntimeConfig,
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

    /// Additional command line arguments passed through to the emulation program.
    /// These are not validated by ffx; they are passed verbatim to the emulator.
    pub kernel_args: Vec<String>,

    /// Zircon Boot image, this is Fuchsia's initial ram disk used in the boot process.
    pub zbi_image: PathBuf,
}

/// Host-side configuration data, such as physical hardware and host OS details.
#[derive(Clone, Debug, Default, Deserialize, PartialEq, Serialize)]
pub struct HostConfig {
    /// Determines the type of hardware acceleration to use for emulation, such as KVM.
    pub acceleration: AccelerationMode,

    /// Determines the type of graphics acceleration, to improve rendering in the guest OS.
    pub gpu: GpuType,

    /// Specifies the path to the emulator's log files.
    pub log: PathBuf,

    /// Determines the networking type for the emulator.
    pub networking: NetworkingMode,
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

    /// Set up the emulation command, print it to the screen, then terminate without
    /// running the emulator. Useful for debugging configuration problems.
    pub dry_run: bool,

    /// Environment variables to set up for the emulation process. Must be of the form
    /// "key=value", or the emulator will reject them.
    pub environment: HashMap<String, String>,

    /// Run the emulator without a GUI. Graphics drivers will still be loaded.
    pub headless: bool,

    /// On machines with high-density screens (such as MacBook Pro), window size may be
    /// scaled to match the host's resolution which results in a much smaller GUI.
    pub hidpi_scaling: bool,

    /// The staging and working directory for the emulator instance.
    pub instance_directory: PathBuf,

    /// The verbosity level of the logs for this instance.
    pub log_level: LogLevel,

    /// The human-readable name for this instance. Must be unique from any other current
    /// instance on the host.
    pub name: String,
}

pub trait BehaviorTrait {
    /// Any code that needs to be executed during shutdown, if this Behavior has been included.
    /// This might include actions like removing files or tearing down network interfaces that
    /// were added in the setup() routine. Returns Ok(()) if everything was torn down successfully.
    /// If it returns an error, that error will be logged and the engine will continue processing
    /// the shutdown command.
    fn cleanup(&self) -> Result<()> {
        log::debug!("SimpleBehavior cleanup");
        Ok(())
    }

    /// Used to decide if a Behavior included in the manifest is appropriate for this emulator
    /// instance. If this function returns Ok(Accept), the Behavior will be included;
    /// Ok(Reject(message)) indicates the Behavior is not included, and the message indicates why.
    /// An Err(_) is exceptional, and generally indicates the emulator should not be launched in
    /// its current state. Examples for filtering include checking command line flag values, host
    /// OS configuration, installed helper programs like KVM or Fuchsia utilities, etc.
    fn filter(&self, _config: &EmulatorConfiguration) -> Result<FilterResult> {
        log::debug!("SimpleBehavior filter");
        Ok(FilterResult::Accept)
    }

    /// Any code that needs to be executed during start, if this Behavior has been included. This
    /// might include actions like resizing or staging additional files, setting up network
    /// interfaces and allocating ports for mapping between the host and guest, etc. If it returns
    /// an error, the start command will stop processing additional Behaviors, the call shutdown
    /// and and run cleanup on any that have already been processed.
    fn setup(&self) -> Result<()> {
        log::debug!("SimpleBehavior setup");
        Ok(())
    }
}
