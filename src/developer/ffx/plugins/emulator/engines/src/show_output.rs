// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! The show_output module contains print routines for the Show subcommand.

use ffx_emulator_common::tuntap::TAP_INTERFACE_NAME;
use ffx_emulator_config::{EmulatorConfiguration, NetworkingMode};

pub(crate) fn net(emu_config: &EmulatorConfiguration) {
    println!("Networking Mode: {}", emu_config.host.networking);
    match emu_config.host.networking {
        NetworkingMode::Tap => {
            println!("  MAC: {}", emu_config.runtime.mac_address);
            println!("  Interface: {}", TAP_INTERFACE_NAME);
            if emu_config.runtime.upscript.is_some() {
                println!(
                    "  Upscript: {}", 
                    emu_config.runtime.upscript.as_ref().unwrap().display()
                );
            }
        }
        NetworkingMode::User => {
            println!("  MAC: {}", emu_config.runtime.mac_address);
            println!("  Ports:");
            for name in emu_config.host.port_map.keys() {
                let ports = emu_config.host.port_map.get(name).unwrap();
                println!(
                    "    {}:\n      guest: {}\n      host: {}", 
                    name,
                    ports.guest,
                    // Every port in the map must be assigned before start-up.
                    ports.host.unwrap(),
                )
            }
        }
        NetworkingMode::Auto |  /* Auto will already be resolved, so skip */
        NetworkingMode::None => /* nothing to add, networking is disabled */ (),
    }
}
