// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
use {
    anyhow::{anyhow, Context as _, Error},
    std::collections::HashMap,
    std::str::Lines,
};

#[derive(Debug)]
struct Device<'a> {
    name: &'a str,
    device: u16,
    subvendor: Option<u16>,
    subdevice: Option<u16>,
}

#[derive(Debug)]
struct Vendor<'a> {
    name: &'a str,
    devices: Vec<Device<'a>>,
}

#[derive(Debug)]
struct SubClass<'a> {
    name: &'a str,
    subclass: u8,
    prog_if: Option<u8>,
}

#[derive(Debug)]
struct Class<'a> {
    name: &'a str,
    subclasses: Vec<SubClass<'a>>,
}

#[derive(Debug)]
pub struct PciDb<'a> {
    device_db: HashMap<u16, Vendor<'a>>,
    class_db: HashMap<u8, Class<'a>>,
}

impl<'a> PciDb<'a> {
    fn count_tabs(line: &str) -> usize {
        let mut tabs = 0;
        let mut chars = line.chars();
        while chars.next() == Some('\t') {
            tabs += 1;
        }
        tabs
    }

    // Parses from the input buffer's iterator until the final vendor id is seen.
    fn parse_devices(iter: &mut Lines<'a>) -> Result<HashMap<u16, Vendor<'a>>, Error> {
        // The flat text DB is represented as a tree, so we can cache vendor
        // and device values to avoid excessive key lookups in the map.
        let mut map: HashMap<u16, Vendor<'a>> = HashMap::new();
        let mut cached_device: u16 = 0;
        let mut cached_vendor: Option<&mut Vendor<'a>> = None;
        for line in iter {
            let trimmed = line.trim();
            let len = trimmed.len();
            // Newlines and comments
            if trimmed.is_empty() || line.starts_with('#') {
                continue;
            }

            // Indent level determines the type of node we are working with.
            match PciDb::count_tabs(line) {
                0 => {
                    // vendor vendor_name
                    // ex: 0014 Loongson Technology LLC
                    let vendor = u16::from_str_radix(&trimmed[0..4], 16)?;
                    let name = &trimmed[6..len];
                    map.insert(vendor, Vendor { name, devices: Vec::new() });
                    if vendor == 0xffff {
                        break;
                    }
                    cached_vendor = map.get_mut(&vendor);
                }
                1 => {
                    // device  device_name
                    // ex: \t0014  Loongson Technology LLC
                    cached_device = u16::from_str_radix(&trimmed[0..4], 16)?;
                    let name = &trimmed[6..len];
                    let v = cached_vendor.unwrap();
                    v.devices.push(Device {
                        name,
                        device: cached_device,
                        subvendor: None,
                        subdevice: None,
                    });
                    cached_vendor = Some(v);
                }
                2 => {
                    // subvendor subdevice  subsystem_name
                    // ex: \t\t001c 0004  2 Channel CAN Bus SJC1000
                    let subvendor = u16::from_str_radix(&trimmed[0..4], 16);
                    let subdevice = u16::from_str_radix(&trimmed[5..9], 16);
                    // Devices with subvendor/subdevice information have an
                    // extra space, but those without do not.
                    let name = &trimmed[10..len].trim();

                    let v = cached_vendor.unwrap();
                    v.devices.push(Device {
                        name,
                        device: cached_device,
                        subvendor: subvendor.ok(),
                        subdevice: subdevice.ok(),
                    });
                    cached_vendor = Some(v);
                }
                _ => return Err(anyhow!("Invalid line in db: \"{}\"", line)),
            }
        }

        Ok(map)
    }

    // Parses the class information out of the iterator, parsing is in a similar format
    // to the vendor:device information, but simpler.
    fn parse_classes(iter: &mut Lines<'a>) -> Result<HashMap<u8, Class<'a>>, Error> {
        let mut map: HashMap<u8, Class<'a>> = HashMap::new();
        let mut cached_subclass: u8 = 0;
        let mut cached_class: Option<&mut Class<'a>> = None;
        for line in iter {
            if line.is_empty() || line.starts_with('#') {
                continue;
            }

            let len = line.len();
            if line.starts_with('C') {
                let class =
                    u8::from_str_radix(&line[2..4], 16).context(format!("'{}'", &line[2..4]))?;
                let name = &line[6..len];
                map.insert(class, Class { name, subclasses: Vec::new() });
                cached_class = map.get_mut(&class);
            } else {
                match PciDb::count_tabs(line) {
                    1 => {
                        cached_subclass = u8::from_str_radix(&line[1..3], 16)
                            .context(format!("'{}'", &line[1..3]))?;
                        let name = &line[5..len];
                        let c = cached_class.unwrap();
                        c.subclasses.push(SubClass {
                            name,
                            subclass: cached_subclass,
                            prog_if: None,
                        });
                        cached_class = Some(c);
                    }
                    2 => {
                        let prog_if = u8::from_str_radix(&line[2..4], 16)
                            .context(format!("'{}'", &line[2..4]))?;
                        let name = &line[6..len];
                        let c = cached_class.unwrap();
                        c.subclasses.push(SubClass {
                            name,
                            subclass: cached_subclass,
                            prog_if: Some(prog_if),
                        });
                        cached_class = Some(c);
                    }

                    _ => return Err(anyhow!("Invalid line in db: \"{}\"", line)),
                }
            }
        }
        Ok(map)
    }

    pub fn new(id_buffer: &'a str) -> Result<Self, Error> {
        let mut iter = id_buffer.lines();
        let device_db = PciDb::parse_devices(&mut iter)?;
        let class_db = PciDb::parse_classes(&mut iter)?;
        Ok(PciDb { device_db, class_db })
    }

    pub fn find_device(
        &self,
        vendor: u16,
        device: u16,
        subvendor: Option<u16>,
        subdevice: Option<u16>,
    ) -> Option<String> {
        if let Some(v_entry) = self.device_db.get(&vendor) {
            let mut name = v_entry.name.to_string();
            // The ID database sorts devices in a manner like:
            // 1000  Broadcom / LSI
            //        0001  53c810
            //        000c  53c895
            //               1000 1010  LSI8951U PCI to Ultra2 SCSI host adapter
            //               1000 1020  LSI8952U PCI to Ultra2 SCSI host adapter
            //               1de1 3906  DC-390U2B SCSI adapter
            //               1de1 3907  DC-390U2W
            // By reversing the list under this vendor/device we grab the most
            // specific name if possible, but fall back on the general device
            // name if none is find for that specific subvendor / subdevice
            // combination.
            for dev in v_entry.devices.iter().rev() {
                if device == dev.device
                    && ((dev.subvendor == None && dev.subdevice == None)
                        || (subvendor == dev.subvendor && subdevice == dev.subdevice))
                {
                    name.push(' ');
                    name.push_str(&dev.name);
                    break;
                }
            }
            return Some(name);
        }
        None
    }

    pub fn find_class(&self, class: u8, subclass: u8, prog_if: Option<u8>) -> Option<String> {
        if let Some(c_entry) = self.class_db.get(&class) {
            let mut name = c_entry.name.to_string();
            for sub in &c_entry.subclasses {
                if subclass == sub.subclass {
                    if sub.prog_if == None {
                        // We've found the basic subclass name, so replace the
                        // base class name we found. A subclass without a program
                        // interface is always the more general class name.
                        name = sub.name.to_string();
                    } else if prog_if.is_some() && prog_if == sub.prog_if {
                        // If we find a matching program interface then append
                        // it to the subclass name found above to get the more
                        // specific name.
                        name.push_str(&format!(" ({})", sub.name));
                    }
                }
            }
            // At the very least we have a base class name. We may also have a subclass name.
            return Some(name);
        }
        None
    }
}

#[cfg(test)]
mod test {
    use super::*;
    use lazy_static::lazy_static;
    use std::fs;

    lazy_static! {
        static ref DB_BUF: String = fs::read_to_string("/pkg/data/lspci/pci.ids")
            .context("Couldn't open PCI IDs file")
            .unwrap();
        static ref DB: PciDb<'static> =
            PciDb::new(&DB_BUF).context("Couldnt parse database buffer").unwrap();
    }

    #[test]
    fn vendor_without_devices() -> Result<(), Error> {
        assert_eq!("SafeNet (wrong ID)", DB.find_device(0x0001, 0, None, None).unwrap());
        // This vendor has no devices, does it still work if we specify one?
        assert_eq!("SafeNet (wrong ID)", DB.find_device(0x0001, 0xFFFF, None, None).unwrap());
        Ok(())
    }

    #[test]
    fn general_device_and_subdevices() -> Result<(), Error> {
        assert_eq!("PEAK-System Technik GmbH", DB.find_device(0x001c, 0, None, None).unwrap());
        assert_eq!(
            "PEAK-System Technik GmbH PCAN-PCI CAN-Bus controller",
            DB.find_device(0x001c, 0x001, None, None).unwrap()
        );
        assert_eq!(
            "PEAK-System Technik GmbH 2 Channel CAN Bus SJC1000",
            DB.find_device(0x001c, 0x001, Some(0x001c), Some(0x0004)).unwrap()
        );
        assert_eq!(
            "PEAK-System Technik GmbH 2 Channel CAN Bus SJC1000 (Optically Isolated)",
            DB.find_device(0x001c, 0x001, Some(0x001c), Some(0x0005)).unwrap()
        );
        Ok(())
    }

    #[test]
    fn numeric_fields_in_entries() -> Result<(), Error> {
        assert_eq!("Broadcom / LSI 53c810", DB.find_device(0x1000, 0x0001, None, None).unwrap());
        assert_eq!(
            "Broadcom / LSI LSI53C810AE PCI to SCSI I/O Processor",
            DB.find_device(0x1000, 0x0001, Some(0x1000), Some(0x1000)).unwrap()
        );
        Ok(())
    }

    #[test]
    fn device_not_found() -> Result<(), Error> {
        // The device ID matches an Allied Telesis, Inc device, but the vendor is invalid.
        assert_eq!(None, DB.find_device(0x0000, 0x8139, None, None));
        // Vendor is LevelOne, but invalid device ID
        assert_eq!("LevelOne", DB.find_device(0x018a, 0x1000, None, None).unwrap());
        // Valid LevelOne FPC-0106TX
        assert_eq!(
            "LevelOne FPC-0106TX misprogrammed [RTL81xx]",
            DB.find_device(0x018a, 0x0106, None, None).unwrap(),
        );
        Ok(())
    }

    struct ClassTest<'a> {
        e: &'a str,
        c: u8,
        s: u8,
        p: Option<u8>,
    }

    #[test]
    fn class_lookup() -> Result<(), Error> {
        let tests = vec![
            ClassTest { e: "Bridge", c: 0x06, s: 0xFF, p: None },
            ClassTest { e: "Bridge", c: 0x06, s: 0xFF, p: Some(0xFF) },
            ClassTest { e: "Bridge", c: 0x06, s: 0x80, p: None },
            ClassTest { e: "Host bridge", c: 0x06, s: 0x00, p: None },
            ClassTest { e: "ISA bridge", c: 0x06, s: 0x01, p: None },
            ClassTest { e: "EISA bridge", c: 0x06, s: 0x02, p: None },
            ClassTest { e: "PCI bridge", c: 0x06, s: 0x04, p: None },
            ClassTest { e: "PCI bridge (Normal decode)", c: 0x06, s: 0x04, p: Some(0x00) },
            ClassTest { e: "PCI bridge (Subtractive decode)", c: 0x06, s: 0x04, p: Some(0x01) },
        ];

        assert_eq!(None, DB.find_class(0xEF, 0x00, None));
        for test in &tests {
            assert_eq!(test.e, DB.find_class(test.c, test.s, test.p).unwrap());
        }
        Ok(())
    }
}
