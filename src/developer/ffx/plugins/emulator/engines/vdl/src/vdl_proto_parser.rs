// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::{format_err, Result};
use regex::Regex;
use std::fs::File;
use std::io::{BufRead, BufReader};
use std::path::PathBuf;

/// Parses VDL Output to get Emulator PID
pub fn get_emu_pid(vdl_output: &PathBuf) -> Result<u32> {
    let reader = BufReader::new(File::open(vdl_output)?);
    let emu_process = Regex::new(r#"\s+name:\s+"Emulator"$"#).unwrap();
    let pid = Regex::new(r"\s+pid:\s+(?P<id>\d+)$").unwrap();

    let mut found_emu = false;
    let mut emu_pid = 0;
    for line in reader.lines() {
        if let Ok(l) = line {
            if !found_emu && emu_process.is_match(&l) {
                found_emu = true;
                continue;
            }
            if found_emu {
                pid.captures(&l).and_then(|cap| {
                    cap.name("id").map(|id| emu_pid = id.as_str().parse::<u32>().unwrap())
                });
                if emu_pid == 0 {
                    break;
                }
                return Ok(emu_pid);
            }
        }
    }
    return Err(format_err!(
        "Cannot parse --vdl-output {} to obtain emulator PID",
        vdl_output.display()
    ));
}

pub fn get_ssh_port(vdl_output: &PathBuf) -> Result<u32> {
    let reader = BufReader::new(File::open(vdl_output)?);
    let name_regex = Regex::new(r#"\s+name:\s+"ssh"$"#).unwrap();
    let value_regex = Regex::new(r"\s+value:\s+(?P<port>\d+)$").unwrap();

    let mut found_ssh = false;
    let mut ssh_port = 0;
    for line in reader.lines() {
        if let Ok(l) = line {
            if !found_ssh && name_regex.is_match(&l) {
                found_ssh = true;
                continue;
            }
            if found_ssh {
                value_regex.captures(&l).and_then(|cap| {
                    cap.name("port").map(|p| ssh_port = p.as_str().parse::<u32>().unwrap())
                });
                if ssh_port == 0 {
                    break;
                }
                return Ok(ssh_port);
            }
        }
    }
    return Err(format_err!(
        "Cannot parse --vdl-output {} to obtain forwarded ssh port",
        vdl_output.display()
    ));
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::io::Write;
    use tempfile::Builder;

    #[test]
    fn test_parse_valid() -> Result<()> {
        let data = format!(
            r#"device_info:  {{
                base_dir:  "/tmp/launcher274142475"
                ports:  {{
                  name:  "ssh"
                  value:  57306
                }}
                ports:  {{
                  name:  "emulatorController"
                  value:  43143
                }}
                processes:  {{
                  name:  "Emulator"
                  pid:  1454638
                }}
                processes:  {{
                  name:  "PackageServer"
                  pid:  1454949
                }}
                device_type:  "workstation_qemu-x64"
              }}
              network_address:  "localhost"
              "#,
        );
        let tmp_dir = Builder::new().prefix("vdl_proto_test_").tempdir()?;
        let vdl_out = tmp_dir.path().join("vdl_output");
        File::create(&vdl_out)?.write_all(data.as_bytes())?;
        let pid = get_emu_pid(&vdl_out)?;
        assert_eq!(pid, 1454638);
        let ssh_port = get_ssh_port(&vdl_out)?;
        assert_eq!(ssh_port, 57306);
        Ok(())
    }

    #[test]
    fn test_parse_error() -> Result<()> {
        let data = format!(
            r#"device_info:  {{
                base_dir:  "/tmp/launcher274142475"
                ports:  {{
                  name:  "ssh"
                  value:
                }}
                ports:  {{
                  name:  "emulatorController"
                  value:  43143
                }}
                processes:  {{
                  name:  "Emulator"
                  pid:
                }}
                processes:  {{
                  name:  "PackageServer"
                  pid:  1454949
                }}
                device_type:  "workstation_qemu-x64"
              }}
              network_address:  "localhost"
              "#,
        );
        let tmp_dir = Builder::new().prefix("vdl_proto_test_").tempdir()?;
        let vdl_out = tmp_dir.path().join("vdl_output");
        File::create(&vdl_out)?.write_all(data.as_bytes())?;
        let pid = get_emu_pid(&vdl_out);
        assert_eq!(pid.is_err(), true);
        let ssh_port = get_ssh_port(&vdl_out);
        assert_eq!(ssh_port.is_err(), true);
        Ok(())
    }
    #[test]
    fn test_parse_no_emu() -> Result<()> {
        let data = format!(
            r#"device_info:  {{
                base_dir:  "/tmp/launcher274142475"
                ports:  {{
                  name:  "emulatorController"
                  value:  43143
                }}
                processes:  {{
                  name:  "PackageServer"
                  pid:  1454949
                }}
                device_type:  "workstation_qemu-x64"
              }}
              network_address:  "localhost"
              "#,
        );
        let tmp_dir = Builder::new().prefix("vdl_proto_test_").tempdir()?;
        let vdl_out = tmp_dir.path().join("vdl_output");
        File::create(&vdl_out)?.write_all(data.as_bytes())?;
        let pid = get_emu_pid(&vdl_out);
        assert_eq!(pid.is_err(), true);
        let ssh_port = get_ssh_port(&vdl_out);
        assert_eq!(ssh_port.is_err(), true);
        Ok(())
    }
}
