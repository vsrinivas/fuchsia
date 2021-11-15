// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! This module holds the common data types for emulator engines. These are implementation-agnostic
//! data types, not the engine-specific command types that each engine will define for itself. These
//! types will be directly deserializable from the PBM, and converted into engine-specific types at
//! runtime.

use anyhow::Result;
use async_trait::async_trait;
use ffx_emulator_common::config::FfxConfigWrapper;
use serde::Deserialize;

#[async_trait]
pub trait EmulatorEngine {
    /// Instantiate an empty instance of this engine type. Meaningful defaults can be applied here,
    /// but the majority of the configuration will occur in initialize(). Should never have a reason
    /// to fail.
    fn new() -> Self
    where
        Self: Sized;

    /// Setup the internal data types from the Product Bundle Manifest structures. This function
    /// will be responsible for validating the input data types, copying the values needed for the
    /// engine's behavior, and performing any other setup required. Could throw an error if the data
    /// isn't valid other setup runs into problems.
    async fn initialize(
        &mut self,
        config: &FfxConfigWrapper,
        emulator_configuration: EmulatorConfiguration,
    ) -> Result<()>;

    /// Start the emulator running. This function shouldn't require any additional configuration as
    /// input, since the object should be fully configured by initialize(). At its most basic, this
    /// should assemble the command-line to invoke the emulator binary, then spawn the process and
    /// exit. If support processes are required, or temporary files need to be written to disk, that
    /// would be handled here. When the function returns, the emulator will be running independently
    /// and the process can terminate, or an error will be sent back explaining the failure.
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
    /// There shouldn't be a case where this fails, since the engine has already been deserialized,
    /// but it returns a Result to allow for things like ffx_bail if its not implemented yet. We
    /// might adjust this later if we find a use case for failing during the show command.
    fn show(&mut self) -> Result<()>;
}

/// EmulatorConfiguration collects the specific configurations into a single struct
/// for ease of passing around.
#[derive(Debug, Default, Deserialize)]
pub struct EmulatorConfiguration {
    pub host: HostConfig,
    pub device: DeviceConfig,
    pub guest: GuestConfig,
    pub runtime: RuntimeConfig,
}

/// This holds the image files and other information specific to the guest OS.
#[derive(Debug, Default, Deserialize)]
pub struct DeviceConfig {}

/// This holds the image files and other information specific to the guest OS.
#[derive(Debug, Default, Deserialize)]
pub struct GuestConfig {}

/// This holds the host-specific configuration data.
#[derive(Debug, Default, Deserialize)]
pub struct HostConfig {}

/// RuntimeConfig is the collection of properties which control/influence the
/// execution of an emulator instance. These are different from the
/// DeviceConfig and GuestConfig which defines the hardware configuration
/// and behavior of Fuchsia running within the emulator instance.
#[derive(Debug, Default, Deserialize)]
pub struct RuntimeConfig {}
