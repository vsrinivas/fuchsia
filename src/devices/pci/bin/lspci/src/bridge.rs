// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
use {
    crate::config::{BridgeControlRegister, SecondaryStatusRegister, Type01Config},
    crate::device::Device,
    crate::util::format_bytes,
    std::fmt,
    zerocopy::LayoutVerified,
};

// A PCI Bridge is a Device with a different (Type 01) configuration layout, but much of the
// information displayed is similar.
pub struct Bridge<'a> {
    device: &'a Device<'a>,
    cfg: LayoutVerified<&'a [u8], Type01Config>,
}

impl<'a> Bridge<'a> {
    pub fn new(device: &'a Device<'_>) -> Self {
        let cfg = Type01Config::new(&device.device.config);
        Bridge { device, cfg }
    }

    pub fn decode_bridge_io(&self) -> Option<(u32, u32)> {
        // Bits 15:4 of both registers reflect the address bits that are writable
        let mut io_base: u32 = (self.cfg.io_base >> 4).into();
        let mut io_limit: u32 = (self.cfg.io_limit >> 4).into();
        let is_32bit: bool = self.cfg.io_base & 0xF == 1;
        // A valid IO base/limit pair is a [io_base, io_limit] range.
        if (io_base == 0 && io_limit == 0) || io_base > io_limit {
            return None;
        }

        // PCI-to-PCI bridge arch spec 3.2.5.6
        // Bridges assume the bottom 12 bits of base are 000
        io_base <<= 12;
        // Bridges assume the bottom 12 bits of limit are 0xFFF
        io_limit <<= 12;
        io_limit |= 0xFFF;
        if is_32bit {
            Some((
                (io_base) | self.cfg.io_base_upper_16 as u32,
                (io_limit) | self.cfg.io_limit_upper_16 as u32,
            ))
        } else {
            Some((io_base, io_limit))
        }
    }

    pub fn decode_bridge_mem(&self) -> Option<(u32, u32)> {
        // Bits 15:4 of both registers reflect the address bits that are writable
        let mut mem_base: u32 = (self.cfg.memory_base >> 4).into();
        let mut mem_limit: u32 = (self.cfg.memory_limit >> 4).into();
        // A valid base/limit pair is a [mem_base, mem_limit] range.
        if (mem_base == 0 && mem_limit == 0) || mem_base > mem_limit {
            return None;
        }

        // PCI-to-PCI bridge arch spec 3.2.5.8
        // Bridges assume the bottom 20 bits of base are 00000
        mem_base <<= 20;
        // Bridges assume the bottom 20 bits of memory limit are 0xFFFFF
        mem_limit <<= 20;
        mem_limit |= 0xFFFFF;
        Some((mem_base, mem_limit))
    }

    pub fn decode_bridge_pf(&self) -> Option<(u64, u64)> {
        // Bits 15:4 of both registers reflect the address bits that are writable
        let mut pf_base: u64 = (self.cfg.pf_memory_base >> 4).into();
        let mut pf_limit: u64 = (self.cfg.pf_memory_limit >> 4).into();
        let is_64bit: bool = self.cfg.pf_memory_base & 0xF == 1;
        // A valid IO base/limit pair is a [pf_base, pf_limit] range.
        if (pf_base == 0 && pf_limit == 0) || pf_base > pf_limit {
            return None;
        }

        // PCI-to-PCI bridge arch spec 3.2.5.9
        // Bridges assume the bottom 20 bits of base are 00000
        pf_base <<= 20;
        // Bridges assume the bottom 20 bits of limit are 0xFFFFF
        pf_limit <<= 20;
        pf_limit |= 0xFFFFF;
        if is_64bit {
            Some((
                (pf_base) | (self.cfg.pf_base_upper_32 as u64) << 32,
                (pf_limit) | (self.cfg.pf_limit_upper_32 as u64) << 32,
            ))
        } else {
            Some((pf_base, pf_limit))
        }
    }
}

// Add a method to device like is_bridge() -> bool that checks the type header
impl<'a> fmt::Display for Bridge<'a> {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        self.device.print_common_header(f)?;
        if self.device.args.verbose {
            writeln!(
            f,
            "\tBus: primary = 0x{:02x}, secondary = 0x{:02x}, subordinate = 0x{:02x}, sec-latency = {}",
            self.cfg.primary_bus_number,
            self.cfg.secondary_bus_number,
            self.cfg.subordinate_bus_number,
            self.cfg.secondary_latency_timer)?;

            write!(f, "\tI/O behind bridge: ")?;
            if let Some((base, limit)) = self.decode_bridge_io() {
                writeln!(
                    f,
                    "[0x{:8x}, 0x{:8x}] [size={}]",
                    base,
                    limit,
                    format_bytes((limit - base + 1) as u64)
                )?;
            } else {
                writeln!(f, "[disabled]")?;
            }
            write!(f, "\tMemory behind bridge: ")?;
            if let Some((base, limit)) = self.decode_bridge_mem() {
                writeln!(
                    f,
                    "[0x{:8x}, 0x{:8x}] [size={}]",
                    base,
                    limit,
                    format_bytes((limit - base + 1) as u64)
                )?;
            } else {
                writeln!(f, "[disabled]")?;
            }
            write!(f, "\tPrefetchable memory behind bridge: ")?;
            if let Some((base, limit)) = self.decode_bridge_pf() {
                writeln!(
                    f,
                    "[0x{:16x}, 0x{:16x}] [size={}]",
                    base,
                    limit,
                    format_bytes((limit - base + 1) as u64)
                )?;
            } else {
                writeln!(f, "[disabled]")?;
            }
            writeln!(
                f,
                "\tSecondary Status: {}",
                SecondaryStatusRegister(self.cfg.secondary_status)
            )?;
            writeln!(f, "\tBridge Control: {}", BridgeControlRegister(self.cfg.bridge_control))?;
        }
        self.device.print_common_footer(f)
    }
}
