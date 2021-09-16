// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{format_err, Result},
    bind::bytecode_encoder::encode_v1::RawInstruction,
    bind::compiler::instruction::DeviceProperty,
    bind::debugger,
    ffx_core::ffx_plugin,
    ffx_driver::{get_device_info, get_driver_info},
    ffx_driver_debug_bind_args::DriverDebugBindCommand,
    fidl_fuchsia_driver_development::{BindRulesBytecode, DriverDevelopmentProxy},
};

#[ffx_plugin(
    "driver_enabled",
    DriverDevelopmentProxy = "bootstrap/driver_manager:expose:fuchsia.driver.development.DriverDevelopment"
)]
pub async fn debug_bind(
    service: DriverDevelopmentProxy,
    cmd: DriverDebugBindCommand,
) -> Result<()> {
    debug_bind_impl(service, cmd, &mut std::io::stdout()).await
}

pub async fn debug_bind_impl<W: std::io::Write>(
    service: DriverDevelopmentProxy,
    cmd: DriverDebugBindCommand,
    writer: &mut W,
) -> Result<()> {
    let driver_info =
        get_driver_info(&service, &mut [cmd.driver_path].iter().map(String::as_str)).await?;

    if driver_info.len() != 1 {
        return Err(format_err!(
            "Unexpected number of results from get_driver_info: {:?}",
            driver_info
        ));
    }

    let bind_rules =
        match driver_info[0].bind_rules.as_ref().ok_or(format_err!("missing bind rules"))? {
            BindRulesBytecode::BytecodeV1(rules) => rules,
            BindRulesBytecode::BytecodeV2(_) => {
                return Err(format_err!("Currently the debugger only supports the old bytecode"));
            }
        };

    let mut device_info =
        get_device_info(&service, &mut [cmd.device_path].iter().map(String::as_str)).await?;

    if device_info.len() != 1 {
        return Err(format_err!(
            "Unexpected number of results from get_device_info: {:?}",
            device_info
        ));
    }

    let raw_instructions = bind_rules
        .into_iter()
        .map(|instruction| RawInstruction([instruction.op, instruction.arg, instruction.debug]))
        .collect::<Vec<RawInstruction<[u32; 3]>>>();

    let device_properties = device_info
        .remove(0)
        .property_list
        .ok_or(format_err!("missing property_list"))?
        .props
        .into_iter()
        .map(DeviceProperty::from)
        .collect::<Vec<DeviceProperty>>();

    if cmd.print_instructions {
        writeln!(writer, "Bind program:")?;
        for instruction in &raw_instructions {
            writeln!(writer, "{}", instruction)?;
        }
        writeln!(writer)?;
    }

    if cmd.print_properties {
        writeln!(writer, "Device properties:")?;
        for property in &device_properties {
            writeln!(writer, "{}", property)?;
        }
        writeln!(writer)?;
    }

    let binds = debugger::debug(&raw_instructions, &device_properties)
        .map_err(|err| format_err!("{}", err))?
        .is_some();

    if binds {
        writeln!(writer, "Driver binds to the device.")?;
    } else {
        writeln!(writer, "Driver doesn't bind to the device.")?;
    }
    Ok(())
}
