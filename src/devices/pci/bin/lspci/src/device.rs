// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
use {
    crate::capability::Capability,
    crate::config::{CommandRegister, StatusRegister, Type00Config},
    crate::db::PciDb,
    crate::util::{format_bytes, Hexdumper},
    crate::Args,
    fidl_fuchsia_hardware_pci::PciDevice as FidlDevice,
    std::fmt,
    zerocopy::LayoutVerified,
};

pub struct Device<'a> {
    pub device: &'a FidlDevice,
    pub class: Option<String>,
    pub name: Option<String>,
    pub cfg: LayoutVerified<&'a [u8], Type00Config>,
    pub args: &'a Args,
}

struct BaseAddress<'a>(&'a fidl_fuchsia_hardware_pci::BaseAddress);
impl<'a> fmt::Display for BaseAddress<'a> {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(
            f,
            "Region {}: {} at {:#x} ({}-bit, {}) [size={}]\n",
            self.0.id,
            if self.0.is_memory { "Memory" } else { "I/O ports" },
            self.0.address,
            if self.0.is_64bit { 64 } else { 32 },
            if self.0.is_prefetchable { "prefetchable" } else { "non-prefetchable" },
            format_bytes(self.0.size)
        )
    }
}

impl<'a> Device<'a> {
    pub fn new(device: &'a FidlDevice, id_db: &Option<PciDb<'_>>, args: &'a Args) -> Self {
        let cfg = Type00Config::new(&device.config);
        let (class, name) = if let Some(db) = id_db {
            (
                db.find_class(cfg.base_class, cfg.sub_class, Some(cfg.program_interface)),
                db.find_device(cfg.vendor_id, cfg.device_id),
            )
        } else {
            (None, None)
        };
        Device { device, class, name, cfg, args }
    }

    pub fn print_common_header(&self, f: &'a mut fmt::Formatter<'_>) -> fmt::Result {
        write!(
            f,
            "{:02x}:{:02x}.{:1x}",
            self.device.bus_id, self.device.device_id, self.device.function_id
        )?;
        if let Some(class) = &self.class {
            if !self.args.only_print_numeric {
                write!(f, " {}", class)?;
            }
        }
        if self.class.is_none() || self.args.print_numeric || self.args.only_print_numeric {
            write!(f, " [{:02x}{:02x}]", self.cfg.base_class, self.cfg.sub_class)?;
        }
        write!(f, ":")?;

        if let Some(name) = &self.name {
            if !self.args.only_print_numeric {
                write!(f, " {}", name)?;
            }
        }

        if self.name.is_none() || self.args.print_numeric || self.args.only_print_numeric {
            write!(f, " [{:04x}:{:04x}]", { self.cfg.vendor_id }, { self.cfg.device_id })?;
        }
        writeln!(f, " (rev {:02x})", self.cfg.revision_id)?;
        if self.args.verbose {
            writeln!(f, "\tControl: {}", CommandRegister(self.cfg.command))?;
            writeln!(f, "\tStatus: {}", StatusRegister(self.cfg.status))?;
        };

        Ok(())
    }

    pub fn print_common_footer(&self, f: &'a mut fmt::Formatter<'_>) -> fmt::Result {
        if self.args.verbose {
            for bar in &self.device.base_addresses {
                if bar.size > 0 {
                    write!(f, "\t{}", BaseAddress(bar))?;
                }
            }
            for capability in &self.device.capabilities {
                writeln!(f, "\t{}", Capability::new(capability, &self.device.config[..]))?;
            }
        }

        if self.args.print_config {
            write!(
                f,
                "{}",
                Hexdumper { bytes: &self.device.config, show_ascii: false, offset: None }
            )?;
        }

        if self.args.verbose || self.args.print_config {
            writeln!(f)?;
        }

        Ok(())
    }
}

impl<'a> fmt::Display for Device<'a> {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        self.print_common_header(f)?;
        self.print_common_footer(f)?;
        Ok(())
    }
}
