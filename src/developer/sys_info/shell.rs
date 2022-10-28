// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::{Context, Error};
use argh::FromArgs;
use fidl_fuchsia_sysinfo::{InterruptControllerInfo, SysInfoMarker, SysInfoProxy};
use fuchsia_component::client::connect_to_protocol;
use fuchsia_zircon as zx;
use futures as _;
use std::fmt::Debug;
use std::io::Write;

#[derive(Debug, Clone, Copy, PartialEq)]
enum SystemInfo {
    BoardName,
    BoardRevision,
    BootLoaderVendor,
    InterruptController,
}

impl std::str::FromStr for SystemInfo {
    type Err = String;

    fn from_str(value: &str) -> Result<Self, Self::Err> {
        match value {
            "board_name" => Ok(SystemInfo::BoardName),
            "board_revision" => Ok(SystemInfo::BoardRevision),
            "bootloader_vendor" => Ok(SystemInfo::BootLoaderVendor),
            "interrupt_controller" => Ok(SystemInfo::InterruptController),
            _ => Err(format!("Invalid key: {:?}. ", value)),
        }
    }
}

fn unwrap_option<T: ToString>(info: Option<T>) -> String {
    info.map(|v| v.to_string()).unwrap_or("None".to_string())
}

fn unwrap_option_debug<T: Debug>(t: Option<T>) -> String {
    t.map(|t| format!("{:?}", t)).unwrap_or("None".to_string())
}

fn write_board_name_info<W: Write>(
    w: &mut W,
    board_name_info: (i32, Option<String>),
) -> Result<(), Error> {
    if zx::Status::from_raw(board_name_info.0) != zx::Status::OK {
        writeln!(w, "zx_status: {}", zx::Status::from_raw(board_name_info.0))?;
    }
    writeln!(w, "board_name: {}", unwrap_option(board_name_info.1))?;
    Ok(())
}

async fn print_board_name_info(provider: SysInfoProxy) -> Result<(), Error> {
    let board_name_info = provider.get_board_name().await?;
    let mut w = std::io::stdout();
    write_board_name_info(&mut w, board_name_info)
        .expect("Function write_board_name_info() failed");
    Ok(())
}

fn write_board_revision_info<W: Write>(
    w: &mut W,
    board_revision_info: (i32, u32),
) -> Result<(), Error> {
    if zx::Status::from_raw(board_revision_info.0) != zx::Status::OK {
        writeln!(w, "zx_status: {}", zx::Status::from_raw(board_revision_info.0))?;
    }
    writeln!(w, "board_revision: {}", board_revision_info.1)?;
    Ok(())
}

async fn print_board_revision_info(provider: SysInfoProxy) -> Result<(), Error> {
    let board_revision_info = provider.get_board_revision().await?;
    let mut w = std::io::stdout();
    write_board_revision_info(&mut w, board_revision_info)
        .expect("Function write_board_revision_info() failed");
    Ok(())
}

fn write_bootloader_vendor_info<W: Write>(
    w: &mut W,
    bootloader_vendor_info: (i32, Option<String>),
) -> Result<(), Error> {
    if zx::Status::from_raw(bootloader_vendor_info.0) != zx::Status::OK {
        writeln!(w, "zx_status: {}", zx::Status::from_raw(bootloader_vendor_info.0))?;
    }
    writeln!(w, "bootloader_vendor: {}", unwrap_option(bootloader_vendor_info.1))?;
    Ok(())
}

async fn print_bootloader_vendor_info(provider: SysInfoProxy) -> Result<(), Error> {
    let bootloader_vendor_info = provider.get_bootloader_vendor().await?;
    let mut w = std::io::stdout();
    write_bootloader_vendor_info(&mut w, bootloader_vendor_info)
        .expect("Function write_bootloader_vendor_info() failed");
    Ok(())
}

fn write_interrupt_controller_info<W: Write>(
    w: &mut W,
    interrupt_controller_info: (i32, Option<Box<InterruptControllerInfo>>),
) -> Result<(), Error> {
    if zx::Status::from_raw(interrupt_controller_info.0) != zx::Status::OK {
        writeln!(w, "zx_status: {}", zx::Status::from_raw(interrupt_controller_info.0))?;
    }
    writeln!(
        w,
        "interrupt_controller: {}",
        unwrap_option_debug(interrupt_controller_info.1.map(|t| t.type_))
    )?;
    Ok(())
}
async fn print_interrupt_controller_info(provider: SysInfoProxy) -> Result<(), Error> {
    let interrupt_controller_info = provider.get_interrupt_controller_info().await?;
    let mut w = std::io::stdout();
    write_interrupt_controller_info(&mut w, interrupt_controller_info)
        .expect("Function write_interrupt_controller_info() failed");
    Ok(())
}

/// System Information command.
#[derive(Debug, PartialEq, FromArgs)]
struct BuildInfoCmd {
    /// valid keys: <board_name> <board_revision> <bootloader_vendor> <interrupt_controller>
    #[argh(positional)]
    info: SystemInfo,
}

#[fuchsia::main]
async fn main() -> Result<(), Error> {
    let args: BuildInfoCmd = argh::from_env();
    let provider = connect_to_protocol::<SysInfoMarker>()
        .context("Failed to connect to the board info service")?;

    match args.info {
        SystemInfo::BoardName => print_board_name_info(provider).await?,
        SystemInfo::BoardRevision => print_board_revision_info(provider).await?,
        SystemInfo::BootLoaderVendor => print_bootloader_vendor_info(provider).await?,
        SystemInfo::InterruptController => print_interrupt_controller_info(provider).await?,
    }

    Ok(())
}

// =========== Testing the sys-info output ===================
#[cfg(test)]
mod tests {
    use super::*;
    use fidl_fuchsia_sysinfo::InterruptControllerType;

    #[fuchsia::test]
    fn test_format_board_name_info() {
        // Tests with the BoardName fields set to data.
        let board_name_info = (0, Some("butterfly".to_string()));
        let mut v = Vec::new();
        write_board_name_info(&mut v, board_name_info)
            .expect("Testing write_board_name_info() failed");
        let write_str = String::from_utf8(v).expect("Failed to convert to string");
        let expect_str = concat!("board_name: butterfly\n");

        // Verify expressions match
        assert_eq!(write_str, expect_str);

        // Tests with the BoardName fields set to None.
        let board_name_info_none = (0, None);
        let mut v_none = Vec::new();
        write_board_name_info(&mut v_none, board_name_info_none)
            .expect("Testing write_board_name_info() failed");
        let write_str_none = String::from_utf8(v_none).expect("Failed to convert to string");
        let expect_str_none = concat!("board_name: None\n");

        // Verify expression match
        assert_eq!(write_str_none, expect_str_none);
    }

    #[fuchsia::test]
    fn test_format_board_revision_info() {
        // Tests with the BoardRevision fields set to data.
        let board_revision_info = (0, 100);
        let mut v = Vec::new();
        write_board_revision_info(&mut v, board_revision_info)
            .expect("Testing write_board_revision_info() failed");
        let write_str = String::from_utf8(v).expect("Failed to convert to string");
        let expect_str = concat!("board_revision: 100\n");

        // Verify expressions match
        assert_eq!(write_str, expect_str);
    }

    #[fuchsia::test]
    fn test_format_bootloader_vendor_info() {
        // Tests with the BootloaderVendor fields set to data.
        let bootloader_vendor_info = (0, Some("vendor-101".to_string()));
        let mut v = Vec::new();
        write_bootloader_vendor_info(&mut v, bootloader_vendor_info)
            .expect("Testing write_bootloader_vendor_info() failed");
        let write_str = String::from_utf8(v).expect("Failed to convert to string");
        let expect_str = concat!("bootloader_vendor: vendor-101\n");

        // Verify expressions match
        assert_eq!(write_str, expect_str);

        // Tests with the BootloaderVendor fields set to None.
        let bootloader_vendor_info_none = (0, None);
        let mut v_none = Vec::new();
        write_bootloader_vendor_info(&mut v_none, bootloader_vendor_info_none)
            .expect("Testing write_bootloader_vendor_info() failed");
        let write_str_none = String::from_utf8(v_none).expect("Failed to convert to string");
        let expect_str_none = concat!("bootloader_vendor: None\n");

        // Verify expression match
        assert_eq!(write_str_none, expect_str_none);
    }

    #[fuchsia::test]
    fn test_format_interrupt_controller_info() {
        // Tests with the InterruptControllerInfo fields set to data.
        let interrupt_info = InterruptControllerInfo { type_: InterruptControllerType::Apic };
        let interrupt_controller_info = (0, Some(Box::new(interrupt_info)));
        let mut v = Vec::new();
        write_interrupt_controller_info(&mut v, interrupt_controller_info)
            .expect("Testing write_interrupt_controller_info() failed");
        let write_str = String::from_utf8(v).expect("Failed to convert to string");
        let expect_str = concat!("interrupt_controller: Apic\n");

        // Verify expressions match
        assert_eq!(write_str, expect_str);

        // Tests with the InterruptControllerInfo fields set to None.
        let interrupt_controller_info_none = (0, None);
        let mut v_none = Vec::new();
        write_interrupt_controller_info(&mut v_none, interrupt_controller_info_none)
            .expect("Testing write_interrupt_controller_info() failed");
        let write_str_none = String::from_utf8(v_none).expect("Failed to convert to string");
        let expect_str_none = concat!("interrupt_controller: None\n");

        // Verify expression match
        assert_eq!(write_str_none, expect_str_none);
    }
}
