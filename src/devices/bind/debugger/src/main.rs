// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use bind_debugger::debugger;
use bind_debugger::encode_bind_program_v1::RawInstruction;
use bind_debugger::instruction::DeviceProperty;
use fidl_fuchsia_device_manager::BindDebuggerMarker;
use fuchsia_async as fasync;
use fuchsia_component::client::connect_to_service;
use fuchsia_zircon as zx;
use structopt::StructOpt;

#[derive(StructOpt, Debug)]
struct Opt {
    /// The path of the driver to debug, e.g. "/system/driver/usb_video.so"
    driver_path: String,

    /// The path of the device to debug, relative to the /dev directory.
    /// E.g. "sys/pci/00:1f.6" or "class/usb-device/000"
    device_path: String,

    /// Print out the device properties.
    #[structopt(short = "p", long = "print-properties")]
    print_properties: bool,

    /// Print out the bind program instructions.
    #[structopt(short = "i", long = "print-instructions")]
    print_instructions: bool,
}

#[fasync::run_singlethreaded]
async fn main() {
    let opt = Opt::from_iter(std::env::args());

    let service = match connect_to_service::<BindDebuggerMarker>() {
        Ok(service) => service,
        Err(_) => {
            eprintln!("Failed to connect to BindDebugger service.");
            std::process::exit(1);
        }
    };

    let bind_program = match service.get_bind_program(&opt.driver_path).await {
        Err(fidl_err) => {
            eprintln!("FIDL call to get bind program failed: {}", fidl_err);
            std::process::exit(1);
        }
        Ok(Err(zx_err)) => {
            eprintln!(
                "FIDL call to get bind program returned an error: {}",
                zx::Status::from_raw(zx_err)
            );
            std::process::exit(1);
        }
        Ok(Ok(program)) => program,
    };

    let device_properties = match service.get_device_properties(&opt.device_path).await {
        Err(fidl_err) => {
            eprintln!("FIDL call to get device properties failed: {}", fidl_err);
            std::process::exit(1);
        }
        Ok(Err(zx_err)) => {
            eprintln!(
                "FIDL call to get device properties returned an error: {}",
                zx::Status::from_raw(zx_err)
            );
            std::process::exit(1);
        }
        Ok(Ok(props)) => props,
    };

    let raw_instructions = bind_program
        .into_iter()
        .map(|instruction| RawInstruction([instruction.op, instruction.arg, instruction.debug]))
        .collect::<Vec<RawInstruction<[u32; 3]>>>();

    let device_properties =
        device_properties.into_iter().map(DeviceProperty::from).collect::<Vec<DeviceProperty>>();

    if opt.print_instructions {
        println!("Bind program:");
        for instruction in &raw_instructions {
            println!("{}", instruction);
        }
        println!();
    }

    if opt.print_properties {
        println!("Device properties:");
        for property in &device_properties {
            println!("{}", property);
        }
        println!();
    }

    let binds = match debugger::debug(&raw_instructions, &device_properties) {
        Ok(properties) => properties.is_some(),
        Err(err) => {
            eprintln!("{}", err);
            std::process::exit(1);
        }
    };

    if binds {
        println!("Driver binds to the device.");
    } else {
        println!("Driver doesn't bind to the device.");
    }
}
