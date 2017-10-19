// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! A Rust library for using the [Zircon software framebuffer](https://goo.gl/tL1Pqi).

// `error_chain!` can recurse deeply
#![recursion_limit = "1024"]
#[macro_use]
extern crate error_chain;
extern crate fuchsia_zircon_sys;
extern crate fdio;

error_chain!{
    foreign_links {
        Io(::std::io::Error);
    }
}

mod color;

pub use color::Color;
use fdio::{ioctl, make_ioctl};
use fdio::fdio_sys::{IOCTL_FAMILY_DISPLAY, IOCTL_KIND_DEFAULT, IOCTL_KIND_GET_HANDLE};
use fuchsia_zircon_sys::{ZX_VM_FLAG_PERM_READ, ZX_VM_FLAG_PERM_WRITE, zx_handle_t, zx_vmar_map,
                         zx_vmar_root_self};
use std::fmt;
use std::fs::File;
use std::io::{self, Read};
use std::mem;
use std::os::unix::io::AsRawFd;
use std::ptr;
use std::thread;

#[repr(C)]
struct zx_display_info_t {
    format: u32,
    width: u32,
    height: u32,
    stride: u32,
    pixelsize: u32,
    flags: u32,
}

const ZX_PIXEL_FORMAT_RGB_565: u32 = 1;
const ZX_PIXEL_FORMAT_ARGB_8888: u32 = 4;
const ZX_PIXEL_FORMAT_RGB_X888: u32 = 5;
const ZX_PIXEL_FORMAT_MONO_1: u32 = 6;
const ZX_PIXEL_FORMAT_MONO_8: u32 = 7;

#[repr(C)]
struct ioctl_display_get_fb_t {
    vmo: zx_handle_t,
    info: zx_display_info_t,
}

/// The native pixel format of the framebuffer.
/// These values are mapped to the values from [zircon/pixelformat.h](https://goo.gl/nM2T7T).
#[derive(Debug, Clone, Copy)]
pub enum PixelFormat {
    Rgb565,
    Argb8888,
    RgbX888,
    Mono1,
    Mono8,
    Unknown,
}

fn get_info_for_device(fd: i32) -> Result<ioctl_display_get_fb_t> {
    let ioctl_display_get_fb_value = make_ioctl(IOCTL_KIND_GET_HANDLE, IOCTL_FAMILY_DISPLAY, 1);

    let mut framebuffer: ioctl_display_get_fb_t = ioctl_display_get_fb_t {
        vmo: 0,
        info: zx_display_info_t {
            format: 0,
            width: 0,
            height: 0,
            stride: 0,
            pixelsize: 0,
            flags: 0,
        },
    };
    let framebuffer_ptr: *mut std::os::raw::c_void = &mut framebuffer as *mut _ as
        *mut std::os::raw::c_void;

    let status = unsafe {
        ioctl(
            fd,
            ioctl_display_get_fb_value,
            ptr::null(),
            0,
            framebuffer_ptr,
            mem::size_of::<ioctl_display_get_fb_t>(),
        )
    };

    if status < 0 {
        if status == -2 {
            bail!(
                "Software framebuffer is not supported on devices with enabled GPU drivers. \
                 See README.md for instructions on how to disable the GPU driver."
            );
        }
        bail!("ioctl failed with {}", status);
    }

    Ok(framebuffer)
}

/// Struct that provides the interface to the Zircon framebuffer.
pub struct FrameBuffer {
    file: File,
    pixel_format: PixelFormat,
    frame_buffer_pixels: Vec<u8>,
    width: usize,
    height: usize,
    stride: usize,
    pixel_size: usize,
}

impl FrameBuffer {
    /// Create a new framebufer. By default this will open the framebuffer
    /// device at /dev/class/framebufer/000 but you can pick a different index.
    /// At the time of this writing, though, there are never any other framebuffer
    /// devices.
    pub fn new(index: Option<isize>) -> Result<FrameBuffer> {
        let index = index.unwrap_or(0);
        let device_path = format!("/dev/class/framebuffer/{:03}", index);
        // driver.usb-audio.disable
        let file = File::open(device_path)?;
        let fd = file.as_raw_fd() as i32;
        let get_fb_data = get_info_for_device(fd)?;
        let pixel_format = match get_fb_data.info.format {
            ZX_PIXEL_FORMAT_RGB_565 => PixelFormat::Rgb565,
            ZX_PIXEL_FORMAT_ARGB_8888 => PixelFormat::Argb8888,
            ZX_PIXEL_FORMAT_RGB_X888 => PixelFormat::RgbX888,
            ZX_PIXEL_FORMAT_MONO_1 => PixelFormat::Mono1,
            ZX_PIXEL_FORMAT_MONO_8 => PixelFormat::Mono8,
            _ => PixelFormat::Unknown,
        };

        let rowbytes = get_fb_data.info.stride * get_fb_data.info.pixelsize;
        let byte_size = rowbytes * get_fb_data.info.height;
        let map_flags = ZX_VM_FLAG_PERM_READ | ZX_VM_FLAG_PERM_WRITE;
        let mut pixel_buffer_addr: usize = 0;
        let pixel_buffer_addr_ptr: *mut usize = &mut pixel_buffer_addr;
        let status = unsafe {
            zx_vmar_map(
                zx_vmar_root_self(),
                0,
                get_fb_data.vmo,
                0,
                byte_size as usize,
                map_flags.bits(),
                pixel_buffer_addr_ptr,
            )
        };

        if status < 0 {
            bail!("zx_vmar_map failed with {}", status);
        }

        let frame_buffer_pixel_ptr = pixel_buffer_addr as *mut u8;
        let frame_buffer_pixels: Vec<u8> = unsafe {
            Vec::from_raw_parts(frame_buffer_pixel_ptr, byte_size as usize, byte_size as usize)
        };

        Ok(FrameBuffer {
            file,
            pixel_format,
            frame_buffer_pixels,
            width: get_fb_data.info.width as usize,
            height: get_fb_data.info.height as usize,
            stride: get_fb_data.info.stride as usize,
            pixel_size: get_fb_data.info.pixelsize as usize,
        })
    }

    /// Call to cause changes you made to the pixel buffer to appear on screen.
    pub fn flush(&self) -> Result<()> {
        let ioctl_display_flush_fb_value = make_ioctl(IOCTL_KIND_DEFAULT, IOCTL_FAMILY_DISPLAY, 2);
        let status = unsafe {
            ioctl(
                self.file.as_raw_fd(),
                ioctl_display_flush_fb_value,
                ptr::null(),
                0,
                ptr::null_mut(),
                0,
            )
        };

        if status < 0 {
            bail!("ioctl failed with {}", status);
        }

        Ok(())
    }

    /// Return the width and height of the framebuffer.
    pub fn get_dimensions(&self) -> (usize, usize) {
        (self.width, self.height)
    }

    /// Return stride of the framebuffer in pixels.
    pub fn get_stride(&self) -> usize {
        self.stride
    }

    /// Return the size in bytes of a pixel pixels.
    pub fn get_pixel_size(&self) -> usize {
        self.pixel_size
    }

    /// Return the size in bytes of a pixel pixels.
    pub fn get_pixel_format(&self) -> PixelFormat {
        self.pixel_format
    }

    /// Return the pixel buffer as a mutable slice.
    pub fn get_pixels(&mut self) -> &mut [u8] {
        self.frame_buffer_pixels.as_mut_slice()
    }
}

impl fmt::Debug for FrameBuffer {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        write!(
            f,
            "FrameBuffer {{ file: {:?}, pixel_format: {:?}, width: {}, height: {}, \
stride: {}, pixel_size: {} }}",
            self.file,
            self.pixel_format,
            self.width,
            self.height,
            self.stride,
            self.pixel_size,
        )
    }
}

/// Convenience function that can be called from main and causes the Fuchsia process being
/// run over ssh to be terminated when the user hits control-C.
pub fn wait_for_close() {
    thread::spawn(move || loop {
        let mut input = [0; 1];
        match io::stdin().read_exact(&mut input) {
            Ok(()) => {}
            Err(_) => std::process::exit(0),
        }
    });
}
