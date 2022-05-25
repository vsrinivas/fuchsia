// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

pub mod args;

use {
    crate::common,
    anyhow::{format_err, Result},
    args::DebugBindCommand,
    bind::{
        bytecode_encoder::encode_v1::RawInstruction, compiler::instruction::DeviceProperty,
        debugger,
    },
    fidl_fuchsia_driver_development as fdd,
    std::io::Write,
};

pub async fn debug_bind(
    cmd: DebugBindCommand,
    writer: &mut impl Write,
    driver_development_proxy: fdd::DriverDevelopmentProxy,
) -> Result<()> {
    let driver_info =
        common::get_driver_info(&driver_development_proxy, &[cmd.driver_path]).await?;

    if driver_info.len() != 1 {
        return Err(format_err!(
            "Unexpected number of results from get_driver_info: {:?}",
            driver_info
        ));
    }

    let bind_rules =
        match driver_info[0].bind_rules.as_ref().ok_or(format_err!("missing bind rules"))? {
            fdd::BindRulesBytecode::BytecodeV1(rules) => rules,
            fdd::BindRulesBytecode::BytecodeV2(_) => {
                return Err(format_err!("Currently the debugger only supports the old bytecode"));
            }
        };

    let mut device_info =
        common::get_device_info(&driver_development_proxy, &[cmd.device_path]).await?;

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
