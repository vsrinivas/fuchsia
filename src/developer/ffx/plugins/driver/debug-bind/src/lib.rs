// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{format_err, Result},
    bind::debugger,
    bind::encode_bind_program_v1::RawInstruction,
    bind::instruction::DeviceProperty,
    ffx_core::ffx_plugin,
    ffx_driver_debug_bind_args::DriverDebugBindCommand,
    fidl_fuchsia_driver_development::{BindRulesBytecode, DriverDevelopmentProxy},
    fuchsia_zircon_status as zx,
};

#[ffx_plugin(
    "driver_enabled",
    DriverDevelopmentProxy = "bootstrap/driver_manager:expose:fuchsia.driver.development.DriverDevelopment"
)]
pub async fn debug_bind(
    service: DriverDevelopmentProxy,
    cmd: DriverDebugBindCommand,
) -> Result<()> {
    let bind_rules_result = service
        .get_bind_rules(&cmd.driver_path)
        .await
        .map_err(|err| format_err!("FIDL call to get bind rules failed: {}", err))?
        .map_err(|err| {
            format_err!(
                "FIDL call to get bind rules returned an error: {}",
                zx::Status::from_raw(err)
            )
        })?;

    let bind_rules = match bind_rules_result {
        BindRulesBytecode::BytecodeV1(rules) => rules,
        BindRulesBytecode::BytecodeV2(_) => {
            return Err(format_err!("Currently the debugger only supports the old bytecode"));
        }
    };

    let device_properties = service
        .get_device_properties(&cmd.device_path)
        .await
        .map_err(|err| format_err!("FIDL call to get device properties failed: {}", err))?
        .map_err(|err| {
            format_err!(
                "FIDL call to get device properties returned an error: {}",
                zx::Status::from_raw(err)
            )
        })?;

    let raw_instructions = bind_rules
        .into_iter()
        .map(|instruction| RawInstruction([instruction.op, instruction.arg, instruction.debug]))
        .collect::<Vec<RawInstruction<[u32; 3]>>>();

    let device_properties = device_properties
        .props
        .into_iter()
        .map(DeviceProperty::from)
        .collect::<Vec<DeviceProperty>>();

    if cmd.print_instructions {
        println!("Bind program:");
        for instruction in &raw_instructions {
            println!("{}", instruction);
        }
        println!();
    }

    if cmd.print_properties {
        println!("Device properties:");
        for property in &device_properties {
            println!("{}", property);
        }
        println!();
    }

    let binds = debugger::debug(&raw_instructions, &device_properties)
        .map_err(|err| format_err!("{}", err))?
        .is_some();

    if binds {
        println!("Driver binds to the device.");
    } else {
        println!("Driver doesn't bind to the device.");
    }
    Ok(())
}
