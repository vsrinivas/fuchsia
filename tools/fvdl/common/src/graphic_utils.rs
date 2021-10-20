// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

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
pub fn get_default_graphics() -> String {
    match env::var_os("DISPLAY") {
        Some(port) => {
            // Running on chrome remote desktop
            if port == ":20" || port.to_string_lossy().starts_with(":20.") {
                return "swiftshader_indirect".to_string();
            }
        }
        // No display support, should use --headless & software-gpu
        None => return "swiftshader_indirect".to_string(),
    };
    let vga = read_vga();
    // If HOST is using Intel or NVIDIA GPU, we generally recommend using
    // "-gpu host" for better graphic performance.
    if !vga.is_empty() && vga.contains("Intel Corporation") {
        return "host".to_string();
    }
    if !vga.is_empty() && vga.contains("NVIDIA Corporation") {
        return "host".to_string();
    }
    return "swiftshader_indirect".to_string();
}

#[cfg(target_os = "macos")]
pub fn get_default_graphics() -> String {
    return "host".to_string();
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
        assert_eq!(get_default_graphics(), "swiftshader_indirect");

        env::set_var("DISPLAY", ":20.0");
        assert_eq!(get_default_graphics(), "swiftshader_indirect");
    }

    #[test]
    #[serial]
    #[cfg(target_os = "linux")]
    fn test_no_display_driver() {
        env::remove_var("DISPLAY");
        assert_eq!(get_default_graphics(), "swiftshader_indirect");
    }
}
