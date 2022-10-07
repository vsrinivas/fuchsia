// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! The config module contains keys to the ffx_config structure for values used
//! by the emulator command.

/// FEMU tool.
pub const FEMU_TOOL: &'static str = "aemu_internal";

/// QEMU tool.
pub const QEMU_TOOL: &'static str = "qemu_internal";

/// The SDK tool "fvm" used to modify fvm files.
pub const FVM_HOST_TOOL: &'static str = "fvm";

/// The SDK tool "zbi" used to modify the zbi image.
pub const ZBI_HOST_TOOL: &'static str = "zbi";

/// The experimental flag for the console subcommand. Defaults to false.
pub const EMU_CONSOLE_FLAG: &'static str = "emu.console.enabled";

/// The default virtual device name to configure the emulator. Defaults to
/// the empty string, but can be overridden by the user.
pub const EMU_DEFAULT_DEVICE: &'static str = "emu.device";

/// The default engine to launch from `ffx emu start`. Defaults to
/// femu, but can be overridden by the user.
pub const EMU_DEFAULT_ENGINE: &'static str = "emu.engine";

/// The default gpu type to use in `ffx emu start`. Defaults to
/// "auto", but can be overridden by the user.
pub const EMU_DEFAULT_GPU: &'static str = "emu.gpu";

/// The root directory for storing instance specific data. Instances
/// should create a subdirectory in this directory to store data.
pub const EMU_INSTANCE_ROOT_DIR: &'static str = "emu.instance_dir";

/// The filesystem path to the system's KVM device. Must be writable by the
/// running process to utilize KVM for acceleration.
pub const KVM_PATH: &'static str = "emu.kvm_path";

/// The duration (in seconds) to attempt to establish an RCS connection with
/// a new emulator before returning to the terminal. Not used in --console or
/// ---monitor modes. Defaults to 60 seconds.
pub const EMU_START_TIMEOUT: &'static str = "emu.start.timeout";

/// The full path to the script to run initializing any network interfaces
/// before starting the emulator.
pub const EMU_UPSCRIPT_FILE: &'static str = "emu.upscript";
