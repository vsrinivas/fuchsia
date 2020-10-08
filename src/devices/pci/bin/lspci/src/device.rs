// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
use {
    crate::capability::Capability,
    crate::config::{CommandRegister, Config, StatusRegister},
    crate::db::PciDb,
    crate::util::format_bytes,
    crate::Args,
    fidl_fuchsia_hardware_pci::Device as FidlDevice,
    std::fmt,
    zerocopy::LayoutVerified,
};

pub struct Device<'a> {
    pub device: &'a FidlDevice,
    pub class: Option<String>,
    pub name: Option<String>,
    cfg: LayoutVerified<&'a [u8], Config>,
    args: &'a Args,
}

impl<'a> Device<'a> {
    pub fn new(device: &'a FidlDevice, id_db: &Option<PciDb<'_>>, args: &'a Args) -> Self {
        let cfg = Config::new(&device.config);
        let (class, name) = if let Some(db) = id_db {
            (
                db.find_class(cfg.base_class, cfg.sub_class, Some(cfg.program_interface)),
                db.find_device(
                    cfg.vendor_id,
                    cfg.device_id,
                    Some(cfg.sub_vendor_id),
                    Some(cfg.subsystem_id),
                ),
            )
        } else {
            (None, None)
        };
        Device { device, class, name, cfg, args }
    }
}

impl<'a> fmt::Display for Device<'a> {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
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
            unsafe {
                write!(f, " [{:04x}:{:04x}]", self.cfg.vendor_id, self.cfg.device_id)?;
            }
        }
        write!(f, " (rev {})\n", self.cfg.revision_id)?;

        if self.args.verbose {
            write!(f, "\t{}\n", CommandRegister(self.cfg.command))?;
            write!(f, "\t{}\n", StatusRegister(self.cfg.status))?;
            for bar in &self.device.base_addresses {
                if bar.size > 0 {
                    write!(
                        f,
                        "\tRegion {}: {} at {:#x} ({}-bit, {}) [size={}]\n",
                        bar.id,
                        if bar.is_memory { "Memory" } else { "I/O ports" },
                        bar.address,
                        if bar.is_64bit { 64 } else { 32 },
                        if bar.is_prefetchable { "prefetchable" } else { "non-prefetchable" },
                        format_bytes(bar.size)
                    )?;
                }
            }
            for capability in &self.device.capabilities {
                write!(f, "\t{}\n", Capability::new(capability, &self.device.config[..]))?;
            }
        }

        if self.args.print_config {
            const SLICE_SIZE: usize = 16;
            for (addr, slice) in self.device.config.chunks(SLICE_SIZE).enumerate() {
                print!("\t{:02x}: ", addr * SLICE_SIZE);
                for byte in slice {
                    print!("{:02x} ", byte);
                }
                print!("\n");
            }
        }

        if self.args.verbose || self.args.print_config {
            write!(f, "\n")?;
        }
        Ok(())
    }
}
