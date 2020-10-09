// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! This is a migration tool to help move from C-macro style bind rules to using the bind compiler.

use regex::Regex;
use std::collections::HashSet;
use std::convert::TryFrom;
use std::fs::File;
use std::fs::OpenOptions;
use std::io::{Read, Seek, SeekFrom, Write};
use std::path::PathBuf;
use structopt::StructOpt;

#[derive(StructOpt, Debug)]
struct Opt {
    #[structopt(parse(from_os_str))]
    input: PathBuf,
}

#[derive(Debug)]
struct DriverArgs {
    driver_name: String,
    driver_ops: String,
    vendor: String,
    version: String,
    num_ops: usize,

    match_start: usize,
    match_end: usize,
}

impl TryFrom<&str> for DriverArgs {
    type Error = &'static str;

    fn try_from(input: &str) -> Result<Self, Self::Error> {
        let begin_re =
            Regex::new(r"ZIRCON_DRIVER_BEGIN\(([^,]*),([^,]*),([^,]*),([^,]*),([^\)]*)\)").unwrap();

        let header = begin_re.captures(input).ok_or("Couldn't find driver begin macro")?;

        let get_group_as_string = |i: usize| -> Result<String, Self::Error> {
            Ok(header.get(i).ok_or("Driver begin macro regex failed")?.as_str().trim().to_string())
        };

        let driver_name = get_group_as_string(1)?;
        let driver_ops = get_group_as_string(2)?;
        let vendor = get_group_as_string(3)?;
        let version = get_group_as_string(4)?;
        let num_ops = get_group_as_string(5)?
            .parse::<usize>()
            .map_err(|_| "Couldn't parse number of bind ops")?;

        let mat = header.get(0).ok_or("Driver begin macro regex failed")?;
        Ok(DriverArgs {
            driver_name,
            driver_ops,
            vendor,
            version,
            num_ops,
            match_start: mat.start(),
            match_end: mat.end(),
        })
    }
}

#[derive(Debug, PartialEq)]
enum Condition {
    Always,
    Equals,
    NotEquals,
    GreaterThan,
    LessThan,
    GreaterThanOrEqual,
    LessThanOrEqual,
}

impl TryFrom<&str> for Condition {
    type Error = &'static str;

    fn try_from(input: &str) -> Result<Self, Self::Error> {
        match input {
            "AL" => Ok(Condition::Always),
            "EQ" => Ok(Condition::Equals),
            "NE" => Ok(Condition::NotEquals),
            "GT" => Ok(Condition::GreaterThan),
            "LT" => Ok(Condition::LessThan),
            "GE" => Ok(Condition::GreaterThanOrEqual),
            "LE" => Ok(Condition::LessThanOrEqual),
            _ => Err("Unrecognised condition"),
        }
    }
}

#[derive(Debug, PartialEq, Eq, Hash)]
enum Library {
    Acpi,
    Bluetooth,
    Composite,
    Pci,
    Platform,
    Serial,
    Test,
    Usb,
    Wlan,
}

impl Library {
    fn name(&self) -> &str {
        match self {
            Library::Acpi => "fuchsia.acpi",
            Library::Bluetooth => "fuchsia.bluetooth",
            Library::Composite => "fuchsia.composite",
            Library::Pci => "fuchsia.pci",
            Library::Platform => "fuchsia.platform",
            Library::Serial => "fuchsia.serial",
            Library::Test => "fuchsia.test",
            Library::Usb => "fuchsia.usb",
            Library::Wlan => "fuchsia.wlan",
        }
    }

    fn build_target(&self) -> &str {
        match self {
            Library::Acpi => "//src/devices/bind/fuchsia.acpi",
            Library::Bluetooth => "//src/devices/bind/fuchsia.bluetooth",
            Library::Composite => "//src/devices/bind/fuchsia.composite",
            Library::Pci => "//src/devices/bind/fuchsia.pci",
            Library::Platform => "//src/devices/bind/fuchsia.platform",
            Library::Serial => "//src/devices/bind/fuchsia.serial",
            Library::Test => "//src/devices/bind/fuchsia.test",
            Library::Usb => "//src/devices/bind/fuchsia.usb",
            Library::Wlan => "//src/devices/bind/fuchsia.wlan",
        }
    }
}

struct Migrator {
    libraries: HashSet<Library>,
}

impl Migrator {
    fn process_build_file(&self, input: PathBuf) -> Result<Vec<PathBuf>, &'static str> {
        let mut output_path = input.clone();
        let mut file = File::open(input).map_err(|_| "Failed to open build file")?;
        let mut contents = String::new();
        file.read_to_string(&mut contents).map_err(|_| "Failed to read build file")?;

        let driver_module_re = Regex::new("driver_module").unwrap();
        let sources_re = Regex::new(r"sources = \[([^\]]*)\]").unwrap();

        let module =
            driver_module_re.find(&contents).ok_or("Couldn't find driver_module in build file")?;
        let sources = sources_re
            .captures(&contents[module.start()..])
            .ok_or("Couldn't find sources in driver_module target")?;

        let source_files = sources.get(1).ok_or("Couldn't find sources in driver_module target")?;
        let mut result = vec![];
        for source in source_files.as_str().split(",") {
            let trimmed = source.trim();
            let unquoted = trimmed.trim_matches('"');
            if !unquoted.is_empty() {
                output_path.set_file_name(unquoted);
                result.push(output_path.clone());
            }
        }
        Ok(result)
    }

    fn insert_build_rule(&self, driver_name: &str, input: PathBuf) -> Result<(), &'static str> {
        let mut file = OpenOptions::new()
            .read(true)
            .write(true)
            .open(input)
            .map_err(|_| "Failed to open build file")?;
        let mut contents = String::new();
        file.read_to_string(&mut contents).map_err(|_| "Failed to read build file")?;

        let import_re = Regex::new(r"import\([^\)]*\)\n").unwrap();
        let mut iter = import_re.find_iter(&contents);
        let first_import = iter.next().ok_or("Couldn't find import list in build file")?;
        let last_import = iter.last().unwrap_or(first_import);

        let driver_module_re = Regex::new("driver_module").unwrap();
        let module =
            driver_module_re.find(&contents).ok_or("Couldn't find driver_module in build file")?;

        let deps_re = Regex::new(r"deps = \[").unwrap();
        let deps =
            deps_re.find(&contents[module.start()..]).ok_or("Couldn't find driver_module deps")?;
        let deps_start = module.start() + deps.start();
        let deps_end = module.start() + deps.end();

        let mut output = String::new();
        output.push_str(&contents[..first_import.start()]);
        output.push_str("import(\"//build/bind/bind.gni\")\n");
        output.push_str(&contents[first_import.start()..last_import.end()]);
        output.push_str("\n");
        output.push_str(format!("bind_rules(\"{}-bind\") {{\n", driver_name).as_str());
        output.push_str(format!("  rules = \"{}.bind\"\n", driver_name).as_str());
        output.push_str(format!("  output = \"{}-bind.h\"\n", driver_name).as_str());
        output.push_str(format!("  tests = \"tests.json\"\n").as_str());
        if !self.libraries.is_empty() {
            output.push_str("  deps = [\n");
            for library in &self.libraries {
                output.push_str(format!("    \"{}\",\n", library.build_target()).as_str());
            }
            output.push_str("  ]\n");
        }
        output.push_str("}\n");
        output.push_str(&contents[last_import.end()..deps_start]);
        output.push_str(format!("deps = [\n    \":{}-bind\",", driver_name).as_str());
        output.push_str(&contents[deps_end..]);

        file.seek(SeekFrom::Start(0)).map_err(|_| "Failed to seek to beginning of build file")?;
        file.set_len(0).map_err(|_| "Failed to truncate build file")?;
        file.write_all(output.as_bytes()).map_err(|_| "Failed to write back to build file")?;

        Ok(())
    }

    // Best effort approach to rename key/values into their bindc equivalents. If the replacement
    // requires a library, keep track of that.
    fn rename<'a>(&mut self, original: &'a str) -> &'a str {
        match original {
            "BIND_PROTOCOL" => "fuchsia.BIND_PROTOCOL",

            "ZX_PROTOCOL_ACPI" => {
                self.libraries.insert(Library::Acpi);
                "fuchsia.acpi.BIND_PROTOCOL.DEVICE"
            }
            "ZX_PROTOCOL_COMPOSITE" => {
                self.libraries.insert(Library::Composite);
                "fuchsia.composite.BIND_PROTOCOL.DEVICE"
            }
            "ZX_PROTOCOL_PCI" => {
                self.libraries.insert(Library::Pci);
                "fuchsia.pci.BIND_PROTOCOL.DEVICE"
            }
            "ZX_PROTOCOL_USB" => {
                self.libraries.insert(Library::Usb);
                "fuchsia.usb.BIND_PROTOCOL.DEVICE"
            }
            "ZX_PROTOCOL_USB_FUNCTION" => {
                self.libraries.insert(Library::Usb);
                "fuchsia.usb.BIND_PROTOCOL.FUNCTION"
            }
            "ZX_PROTOCOL_SERIAL" => {
                self.libraries.insert(Library::Serial);
                "fuchsia.serial.BIND_PROTOCOL.DEVICE"
            }
            "ZX_PROTOCOL_SERIAL_IMPL" => {
                self.libraries.insert(Library::Serial);
                "fuchsia.serial.BIND_PROTOCOL.IMPL"
            }
            "ZX_PROTOCOL_SERIAL_IMPL_ASYNC" => {
                self.libraries.insert(Library::Serial);
                "fuchsia.serial.BIND_PROTOCOL.IMPL_ASYNC"
            }
            "ZX_PROTOCOL_TEST" => {
                self.libraries.insert(Library::Test);
                "fuchsia.test.BIND_PROTOCOL.DEVICE"
            }
            "ZX_PROTOCOL_TEST_COMPAT_CHILD" => {
                self.libraries.insert(Library::Test);
                "fuchsia.test.BIND_PROTOCOL.COMPAT_CHILD"
            }
            "ZX_PROTOCOL_TEST_POWER_CHILD" => {
                self.libraries.insert(Library::Test);
                "fuchsia.test.BIND_PROTOCOL.POWER_CHILD"
            }
            "ZX_PROTOCOL_TEST_PARENT" => {
                self.libraries.insert(Library::Test);
                "fuchsia.test.BIND_PROTOCOL.PARENT"
            }
            "ZX_PROTOCOL_WLANPHY" => {
                self.libraries.insert(Library::Wlan);
                "fuchsia.wlan.BIND_PROTOCOL.PHY"
            }
            "ZX_PROTOCOL_WLANPHY_IMPL" => {
                self.libraries.insert(Library::Wlan);
                "fuchsia.wlan.BIND_PROTOCOL.PHY_IMPL"
            }
            "ZX_PROTOCOL_WLANIF" => {
                self.libraries.insert(Library::Wlan);
                "fuchsia.wlan.BIND_PROTOCOL.IF"
            }
            "ZX_PROTOCOL_WLANIF_IMPL" => {
                self.libraries.insert(Library::Wlan);
                "fuchsia.wlan.BIND_PROTOCOL.IF_IMPL"
            }
            "ZX_PROTOCOL_WLANMAC" => {
                self.libraries.insert(Library::Wlan);
                "fuchsia.wlan.BIND_PROTOCOL.MAC"
            }
            "ZX_PROTOCOL_BT_HCI" => {
                self.libraries.insert(Library::Bluetooth);
                "fuchsia.bluetooth.BIND_PROTOCOL.HCI"
            }
            "ZX_PROTOCOL_BT_EMULATOR" => {
                self.libraries.insert(Library::Bluetooth);
                "fuchsia.bluetooth.BIND_PROTOCOL.EMULATOR"
            }
            "ZX_PROTOCOL_BT_TRANSPORT" => {
                self.libraries.insert(Library::Bluetooth);
                "fuchsia.bluetooth.BIND_PROTOCOL.TRANSPORT"
            }
            "ZX_PROTOCOL_BT_HOST" => {
                self.libraries.insert(Library::Bluetooth);
                "fuchsia.bluetooth.BIND_PROTOCOL.HOST"
            }
            "ZX_PROTOCOL_BT_GATT_SVC" => {
                self.libraries.insert(Library::Bluetooth);
                "fuchsia.bluetooth.BIND_PROTOCOL.GATT_SVC"
            }

            "BIND_PCI_VID" => {
                self.libraries.insert(Library::Pci);
                "fuchsia.BIND_PCI_VID"
            }
            "BIND_PCI_DID" => {
                self.libraries.insert(Library::Pci);
                "fuchsia.BIND_PCI_DID"
            }

            "BIND_USB_DID" => {
                self.libraries.insert(Library::Usb);
                "fuchsia.BIND_USB_DID"
            }
            "BIND_USB_PID" => {
                self.libraries.insert(Library::Usb);
                "fuchsia.BIND_USB_PID"
            }
            "BIND_USB_CLASS" => {
                self.libraries.insert(Library::Usb);
                "fuchsia.BIND_USB_CLASS"
            }
            "BIND_USB_SUBCLASS" => {
                self.libraries.insert(Library::Usb);
                "fuchsia.BIND_USB_SUBCLASS"
            }

            "BIND_PLATFORM_DEV_VID" => {
                self.libraries.insert(Library::Platform);
                "fuchsia.BIND_PLATFORM_DEV_VID"
            }
            "BIND_PLATFORM_DEV_PID" => "fuchsia.BIND_PLATFORM_DEV_PID",
            "BIND_PLATFORM_DEV_DID" => "fuchsia.BIND_PLATFORM_DEV_DID",

            "ATHEROS_VID" => "fuchsia.pci.BIND_PCI_VID.ATHEROS",
            "INTEL_VID" => "fuchsia.pci.BIND_PCI_VID.INTEL",

            _ => original,
        }
    }

    fn process_source_file(&mut self, input: PathBuf) -> Result<Option<String>, &'static str> {
        let mut file = OpenOptions::new()
            .read(true)
            .write(true)
            .open(input.clone())
            .map_err(|_| "Failed to open build file")?;
        let mut contents = String::new();
        file.read_to_string(&mut contents).map_err(|_| "Failed to read build file")?;

        // These regular expressions will fail if there are additional parentheses or commas in the
        // macros, but we're okay with that since this whole tool is best effort.
        let include_re = Regex::new(r"(?m)^#include <ddk/binding.h>$").unwrap();
        let end_re = Regex::new(r"ZIRCON_DRIVER_END\([^\)]*\)").unwrap();
        let op_re = Regex::new(r"BI_[A-Z_]*\([^\)]*\)").unwrap();
        let abort_re = Regex::new(r"BI_ABORT\(\)").unwrap();
        let abort_if_re = Regex::new(r"BI_ABORT_IF\(([A-Z][A-Z]),([^,]*),([^\)]*)\)").unwrap();
        let match_if_re = Regex::new(r"BI_MATCH_IF\(([A-Z][A-Z]),([^,]*),([^\)]*)\)").unwrap();

        let args = DriverArgs::try_from(contents.as_str());
        if !args.is_ok() {
            return Ok(None);
        }
        let args = args?;

        let mut bind_rules = String::new();
        let mut count = 0;
        let mut iter = op_re.find_iter(&contents);
        while let Some(mat) = iter.next() {
            if abort_re.is_match(mat.as_str()) {
                bind_rules.push_str("abort;")
            } else if let Some(caps) = abort_if_re.captures(mat.as_str()) {
                let condition = Condition::try_from(caps.get(1).unwrap().as_str());
                let lhs = self.rename(caps.get(2).unwrap().as_str().trim());
                let rhs = self.rename(caps.get(3).unwrap().as_str().trim());
                let rule = match condition {
                    Ok(Condition::Equals) => Ok(format!("{} != {};\n", lhs, rhs)),
                    Ok(Condition::NotEquals) => Ok(format!("{} == {};\n", lhs, rhs)),
                    _ => Err("Unsupported condition"),
                }?;
                bind_rules.push_str(rule.as_str());
            } else if let Some(caps) = match_if_re.captures(mat.as_str()) {
                let condition = Condition::try_from(caps.get(1).unwrap().as_str());
                let first_lhs = caps.get(2).unwrap().as_str().trim();
                let rhs = self.rename(caps.get(3).unwrap().as_str().trim());

                bind_rules.push_str(format!("accept {} {{\n", self.rename(first_lhs)).as_str());
                bind_rules.push_str(format!("  {},\n", rhs).as_str());

                // We only support BI_MATCH_IF when we can convert it to an accept. So every remaining
                // op must be BI_MATCH_IF with the same LHS. We also only support EQ as the condition.
                assert_eq!(condition, Ok(Condition::Equals));
                while let Some(mat) = iter.next() {
                    if let Some(caps) = match_if_re.captures(mat.as_str()) {
                        let condition = Condition::try_from(caps.get(1).unwrap().as_str());
                        assert_eq!(condition, Ok(Condition::Equals));

                        let lhs = caps.get(2).unwrap().as_str().trim();
                        let rhs = self.rename(caps.get(3).unwrap().as_str().trim());
                        assert_eq!(lhs, first_lhs);
                        bind_rules.push_str(format!("  {},\n", rhs).as_str());
                    } else {
                        return Err("Unsupported bind rules");
                    }
                    count += 1;
                }
                bind_rules.push_str("}\n");
            } else {
                println!("The migration tool doesn't handle this bind op: {}", mat.as_str());
                return Err("Unhandled bind op");
            }

            count += 1;
        }

        assert_eq!(count, args.num_ops);

        let mut source_output = String::new();

        let mut bind_output_file_path = input.clone();
        bind_output_file_path.set_file_name(format!("{}-bind.h", args.driver_name).as_str());

        let include = include_re.find(&contents).ok_or("Couldn't find binding include")?;
        source_output.push_str(&contents[..include.start()]);
        if let Some(full_path) = bind_output_file_path.to_str() {
            source_output.push_str(format!("#include \"{}\"\n", full_path).as_str());
        } else {
            source_output.push_str(format!("#include \"{}-bind.h\"\n", args.driver_name).as_str());
        }

        source_output.push_str(&contents[include.end()..args.match_start]);
        source_output.push_str(
            format!(
                "ZIRCON_DRIVER({}, {}, {}, {})\n",
                args.driver_name, args.driver_ops, args.vendor, args.version
            )
            .as_str(),
        );

        let end = end_re.find(&contents).ok_or("Couldn't find end of driver macro")?;
        source_output.push_str(&contents[end.end()..]);

        file.seek(SeekFrom::Start(0)).map_err(|_| "Failed to seek to beginning of source file")?;
        file.set_len(0).map_err(|_| "Failed to truncate source file")?;
        file.write_all(source_output.as_bytes()).unwrap();

        let mut bind_file_data = String::new();
        bind_file_data.push_str("// Copyright 2020 The Fuchsia Authors. All rights reserved.\n");
        bind_file_data.push_str(
            "// Use of this source code is governed by a BSD-style license that can be\n",
        );
        bind_file_data.push_str("// found in the LICENSE file.\n\n");

        for library in &self.libraries {
            bind_file_data.push_str(format!("using {};\n", library.name()).as_str());
        }

        if !self.libraries.is_empty() {
            bind_file_data.push_str("\n");
        }
        bind_file_data.push_str(bind_rules.as_str());

        let mut bind_file_path = input;
        bind_file_path.set_file_name(format!("{}.bind", args.driver_name).as_str());
        let mut bind_file = OpenOptions::new()
            .read(true)
            .write(true)
            .create(true)
            .open(bind_file_path)
            .map_err(|_| "Failed to open bind file")?;
        bind_file
            .write_all(bind_file_data.as_bytes())
            .map_err(|_| "Failed to write to bind file")?;

        Ok(Some(args.driver_name))
    }
}

fn main() -> Result<(), &'static str> {
    let opt = Opt::from_iter(std::env::args());

    let mut migrator = Migrator { libraries: HashSet::new() };
    for source_file in migrator.process_build_file(opt.input.clone())? {
        if let Some(source_file_str) = source_file.to_str() {
            println!("Checking {}", source_file_str);
        }
        let processed = migrator.process_source_file(source_file)?;
        if let Some(driver_name) = processed {
            migrator.insert_build_rule(&driver_name, opt.input)?;
            return Ok(());
        }
    }

    Err("Couldn't find a ZIRCON_DRIVER_BEGIN macro")
}
