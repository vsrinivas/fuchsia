// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Result,
    ffx_core::ffx_plugin,
    ffx_driver::get_driver_info,
    ffx_driver_list_args::DriverListCommand,
    fidl_fuchsia_driver_development::{BindRulesBytecode, DriverDevelopmentProxy},
};

#[ffx_plugin(
    "driver_enabled",
    DriverDevelopmentProxy = "bootstrap/driver_manager:expose:fuchsia.driver.development.DriverDevelopment"
)]
pub async fn list(service: DriverDevelopmentProxy, cmd: DriverListCommand) -> Result<()> {
    let driver_info = get_driver_info(&service, &mut [].iter().map(String::as_str)).await?;

    if cmd.verbose {
        for driver in driver_info {
            println!("{0: <10}: {1}", "Name", driver.name.unwrap_or("".to_string()));
            if let Some(libname) = driver.libname {
                println!("{0: <10}: {1}", "Driver", libname);
            }
            if let Some(url) = driver.url {
                println!("{0: <10}: {1}", "URL", url);
            }
            match driver.bind_rules {
                Some(BindRulesBytecode::BytecodeV1(bytecode)) => {
                    println!("{0: <10}: {1}", "Bytecode Version", 1);
                    println!("{0: <10}({1} bytes): {2:?}", "Bytecode:", bytecode.len(), bytecode);
                }
                Some(BindRulesBytecode::BytecodeV2(bytecode)) => {
                    println!("{0: <10}: {1}", "Bytecode Version", 2);
                    println!("{0: <10}({1} bytes): {2:?}", "Bytecode:", bytecode.len(), bytecode);
                }
                _ => println!("{0: <10}: {1}", "Bytecode Version", "Unknown"),
            }
            println!();
        }
    } else {
        for driver in driver_info {
            let name = driver.name.unwrap_or("<unknown>".to_string());
            let libname_or_url = driver.libname.or(driver.url).unwrap_or("".to_string());

            println!("{:<20}: {}", name, libname_or_url);
        }
    }
    Ok(())
}
