// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use argh::FromArgs;
use ffx_core::ffx_command;
use ffx_emulator_config::EngineConsoleType;

#[ffx_command()]
#[derive(FromArgs, Default, Debug, PartialEq)]
#[argh(
    subcommand,
    name = "console",
    description = "[EXPERIMENTAL] Connect to a running Fuchsia emulator's console.",
    example = "ffx emu console -s
ffx emu console fuchsia-emulator --console-type serial"
)]

// While the command is experimental, we're maintaining two types of flags here, to see which works
// better. The console_type flag lets the user select from an enum, while command/machine/serial
// provide single-letter quick options. We'll likely remove one of the options before removing the
// experimental flag.
pub struct ConsoleCommand {
    /// name of the emulator to connect to, as specified to the start command.
    /// See a list of available instances by running `ffx emu list`. If no name
    /// is specified, and only one emulator is running, it will be selected.
    #[argh(positional)]
    pub name: Option<String>,

    /// selector for which console to attach to. Accepted values are:
    ///     command
    ///     machine
    ///     serial
    #[argh(option, default = "EngineConsoleType::None")]
    pub console_type: EngineConsoleType,

    // The "command" flag is for the Qemu Monitor and similar consoles. This console is explicitly
    // interactive, used by users retrieve information about and modify the state of the hypervisor
    // (as opposed to the guest system it's running). It might be used to alter the virtual
    // hardware while the system is live (e.g. adding/removing virtual USB devices), or to issue
    // commands to the system (e.g. shutdown/snapshot/etc.). For more information about the Qemu
    // Monitor and its capabilities, see https://www.qemu.org/docs/master/system/monitor.html.
    /// attach to the user-interactive command console. Equivalent to "--console-type command".
    #[argh(switch, short = 'c')]
    pub command: bool,

    // The "machine" flag is for the Qemu Monitor Protocol (QMP) and similar consoles. This console
    // is for machine-to-machine interaction, as opposed to the Monitor which is meant for human
    // interaction. The QMP is more complex than the Monitor, including asynchronous event
    // reporting which can break simple request/response systems, but can be used for many of the
    // same purposes as the Monitor. For more information about the QMP and its capabilities, see
    // https://www.qemu.org/docs/master/interop/qemu-qmp-ref.html.
    /// attach to the machine-readable command console. Equivalent to "--console-type machine".
    #[argh(switch, short = 'm')]
    pub machine: bool,

    // The "serial" flag is for the Fuchsia serial port. Fuchsia exposes an interactive console on
    // each device's serial port, where it also dumps system logs. This console has the same
    // capabilities as the console accessed by connecting to the device via SSH.
    /// attach to the Fuchsia serial console. Equivalent to "--console-type serial".
    #[argh(switch, short = 's')]
    pub serial: bool,
}
