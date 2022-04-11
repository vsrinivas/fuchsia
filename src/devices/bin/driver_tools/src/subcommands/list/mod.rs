// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

pub mod args;

use {
    super::common, anyhow::Result, args::ListCommand,
    fidl_fuchsia_driver_development::BindRulesBytecode, futures::join, std::collections::HashSet,
};

pub async fn list(
    remote_control: fidl_fuchsia_developer_remotecontrol::RemoteControlProxy,
    cmd: ListCommand,
) -> Result<()> {
    let service = common::get_development_proxy(remote_control, cmd.select).await?;
    let empty: [String; 0] = [];
    let driver_info = common::get_driver_info(&service, &empty);

    let driver_info = if cmd.loaded {
        // Query devices and create a hash set of loaded drivers.
        let device_info = common::get_device_info(&service, &empty);

        // Await the futures concurrently.
        let (driver_info, device_info) = join!(driver_info, device_info);

        let mut loaded_driver_set = HashSet::new();
        for device in device_info? {
            if let Some(libname) = &device.bound_driver_libname {
                loaded_driver_set.insert(String::from(libname));
            }
        }

        // Filter the driver list by the hash set.
        driver_info?
            .into_iter()
            .filter(|driver| {
                if let Some(libname_or_url) = driver.libname.as_ref().or(driver.url.as_ref()) {
                    loaded_driver_set.contains(libname_or_url)
                } else {
                    false
                }
            })
            .collect()
    } else {
        driver_info.await?
    };

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
