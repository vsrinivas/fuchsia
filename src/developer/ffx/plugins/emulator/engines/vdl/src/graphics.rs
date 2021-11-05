// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use ffx_emulator_start_args::GpuType;

pub fn get_default_graphics() -> GpuType {
    return get_default_graphics_for_os();
}

#[cfg(target_os = "linux")]
use {std::env, std::process::Command, std::str};

#[cfg(target_os = "linux")]
pub fn read_vga() -> String {
    match Command::new("lspci").output() {
        Ok(output) => {
            if output.status.success() {
                for pci in str::from_utf8(&output.stdout).unwrap().lines() {
                    if pci.contains("VGA") {
                        return pci.to_string();
                    }
                }
            }
            return "".to_string();
        }
        Err(_) => return "".to_string(),
    };
}

#[cfg(target_os = "linux")]
fn get_default_graphics_for_os() -> GpuType {
    match env::var_os("DISPLAY") {
        Some(port) => {
            // Running on chrome remote desktop
            if port == ":20" || port.to_string_lossy().starts_with(":20.") {
                return GpuType::SwiftshaderIndirect;
            }
        }
        // No display support, should use --headless & software-gpu
        None => return GpuType::SwiftshaderIndirect,
    };
    let vga = read_vga();
    // If HOST is using Intel or NVIDIA GPU, we generally recommend using
    // "-gpu host" for better graphic performance.
    if !vga.is_empty() && vga.contains("Intel Corporation") {
        return GpuType::Host;
    }
    if !vga.is_empty() && vga.contains("NVIDIA Corporation") {
        return GpuType::Host;
    }
    return GpuType::SwiftshaderIndirect;
}

#[cfg(target_os = "macos")]
fn get_default_graphics_for_os() -> GpuType {
    return GpuType::Host;
}

#[cfg(test)]
mod tests {

    #[cfg(target_os = "linux")]
    use {super::*, serial_test::serial};

    #[test]
    #[serial]
    #[cfg(target_os = "linux")]
    fn test_read_vga() {
        let vga = read_vga();
        if !vga.is_empty() {
            assert!(vga.contains("VGA"));
        }
    }

    #[test]
    #[serial]
    #[cfg(target_os = "linux")]
    fn test_crd() {
        env::set_var("DISPLAY", ":20");
        assert_eq!(get_default_graphics(), GpuType::SwiftshaderIndirect);

        env::set_var("DISPLAY", ":20.0");
        assert_eq!(get_default_graphics(), GpuType::SwiftshaderIndirect);
    }

    #[test]
    #[serial]
    #[cfg(target_os = "linux")]
    fn test_no_display_driver() {
        env::remove_var("DISPLAY");
        assert_eq!(get_default_graphics(), GpuType::SwiftshaderIndirect);
    }
}
