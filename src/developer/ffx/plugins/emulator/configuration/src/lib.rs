// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! This module holds the common data types for emulator engines. These are implementation-agnostic
//! data types, not the engine-specific command types that each engine will define for itself. These
//! types will be directly deserializable from the PBM, and converted into engine-specific types at
//! runtime.

use serde::Deserialize;

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
    fn initialize(
        &mut self,
        device_config: DeviceConfig,
        guest_config: GuestConfig,
        host_config: HostConfig,
        runtime_config: RuntimeConfig,
    ) -> Result<(), anyhow::Error>;

    /// Start the emulator running. This function shouldn't require any additional configuration as
    /// input, since the object should be fully configured by initialize(). At its most basic, this
    /// should assemble the command-line to invoke the emulator binary, then spawn the process and
    /// exit. If support processes are required, or temporary files need to be written to disk, that
    /// would be handled here. When the function returns, the emulator will be running independently
    /// and the process can terminate, or an error will be sent back explaining the failure.
    fn start(&mut self) -> Result<i32, anyhow::Error>;

    /// Shut down a running emulator instance. The engine should have been instantiated from a saved
    /// and serialized instance, so no additional initialization should be needed. This function
    /// will terminate a running emulator instance, which will be specified on the command line. It
    /// may return an error if the instance doesn't exist or the shut down fails, but should succeed
    /// if it's no longer running or gets successfully shut down.
    fn shutdown(&mut self) -> Result<(), anyhow::Error>;

    /// Output the details of an existing emulation instance. The engine should have been
    /// instantiated from a saved and serialized instance, so no additional initialization should be
    /// needed. This function will output text to the terminal describing the instance, its status,
    /// and its configuration. This is an engine-specific output with more detail than `ffx list`.
    /// There shouldn't be a case where this fails, since the engine has already been deserialized,
    /// but it returns a Result to allow for things like ffx_bail if its not implemented yet. We
    /// might adjust this later if we find a use case for failing during the show command.
    fn show(&mut self) -> Result<(), anyhow::Error>;
}

/// This holds the image files and other information specific to the guest OS.
#[derive(Debug, Deserialize)]
pub struct DeviceConfig {}

/// This holds the image files and other information specific to the guest OS.
#[derive(Debug, Deserialize)]
pub struct GuestConfig {}

/// This holds the engine-specific configuration data.
#[derive(Debug, Deserialize)]
pub struct HostConfig {}

/// This holds the image files and other information specific to the guest OS.
#[derive(Debug, Deserialize)]
pub struct RuntimeConfig {}
