// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::framebuffer::{DetectResult, DisplayInfo, Framebuffer},
    anyhow::Error,
    libc,
    serde::Serialize,
    serde_json::json,
    std::fs::File,
    std::os::unix::io::AsRawFd,
};

// Linux framebuffer IOCTLs taken from linux/fb.h
const FBIOGET_VSCREENINFO: libc::c_ulong = 0x4600;
const FBIOGET_FSCREENINFO: libc::c_ulong = 0x4602;

#[derive(Debug, Default, Copy, Clone, Serialize)]
#[repr(C, packed)]
pub struct FbFixScreeninfo {
    id: [u8; 16],
    smem_start: libc::c_ulong,
    smem_len: u32,
    r#type: u32,
    type_aux: u32,
    visual: u32,
    xpanstep: u16,
    ypanstep: u16,
    ywrapstep: u16,
    line_length: u32,
    mmio_start: libc::c_ulong,
    mmio_len: u32,
    accel: u32,
    capabilities: u16,
    reserved: [u16; 2],
}

#[derive(Debug, Default, Copy, Clone, Serialize)]
#[repr(C, packed)]
pub struct FbBitfield {
    offset: u32,
    length: u32,
    msb_right: u32,
}

#[derive(Debug, Default, Copy, Clone, Serialize)]
#[repr(C, packed)]
pub struct FbVarScreeninfo {
    xres: u32,
    yres: u32,
    xres_virtual: u32,
    yres_virtual: u32,
    xoffset: u32,
    yoffset: u32,
    bits_per_pixel: u32,
    grayscale: u32,
    red: FbBitfield,
    green: FbBitfield,
    blue: FbBitfield,
    transp: FbBitfield,
    nonstd: u32,
    activate: u32,
    height: u32,
    width: u32,
    accel_flags: u32,
    pixclock: u32,
    left_margin: u32,
    right_margin: u32,
    upper_margin: u32,
    lower_margin: u32,
    hsync_len: u32,
    vsync_len: u32,
    sync: u32,
    vmode: u32,
    rotate: u32,
    colorspace: u32,
    reserved: [u32; 4],
}

fn linuxfb_info_from_file(file: File) -> Result<DetectResult, Error> {
    let mut vinfo: FbVarScreeninfo = Default::default();
    let mut finfo: FbFixScreeninfo = Default::default();
    let ret = unsafe { libc::ioctl(file.as_raw_fd(), FBIOGET_VSCREENINFO, &mut vinfo) };
    if ret != 0 {
        return Err(std::io::Error::last_os_error().into());
    }
    let ret = unsafe { libc::ioctl(file.as_raw_fd(), FBIOGET_FSCREENINFO, &mut finfo) };
    if ret != 0 {
        return Err(std::io::Error::last_os_error().into());
    }
    Ok(DetectResult {
        displays: vec![DisplayInfo {
            id: String::from_utf8_lossy(&finfo.id).into(),
            width: vinfo.xres_virtual,
            height: vinfo.yres_virtual,
        }],
        details: json!({
            "fb_var_screeninfo": vinfo,
            "fb_fix_screeninfo": finfo,
        }),
        ..Default::default()
    })
}

fn read_dev_fb0_info() -> DetectResult {
    match File::open("/dev/fb0") {
        Ok(f) => linuxfb_info_from_file(f).unwrap_or_else(DetectResult::from_error),
        Err(e) => DetectResult::from_error(e.into()),
    }
}

pub struct LinuxFramebuffer;

impl Framebuffer for LinuxFramebuffer {
    fn detect_displays(&self) -> DetectResult {
        read_dev_fb0_info()
    }
}
