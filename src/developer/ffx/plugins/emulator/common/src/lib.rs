// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! This library contains the shared functions used by multiple emulation engines. Any code placed
//! in this library may not depend on any other code within the plugin, with the exception of "args"
//! libraries.

use anyhow::{anyhow, Context, Result};
use nix::{
    ifaddrs::getifaddrs,
    net::if_::InterfaceFlags,
    sys::socket::{InetAddr, SockAddr},
};
use std::{
    fs::File,
    io::{BufRead, Write},
    path::PathBuf,
};

// Provides access to ffx_config properties.
pub mod config;
pub mod instances;
pub mod process;
pub mod target;
pub mod tuntap;

/// A utility function for checking whether the host OS is MacOS.
pub fn host_is_mac() -> bool {
    std::env::consts::OS == "macos"
}

/// A utility function for splitting a string at a single point and converting it into a tuple.
/// Returns an Err(anyhow) if it can't do the split.
pub fn split_once(text: &str, pattern: &str) -> Result<(String, String)> {
    let splitter: Vec<&str> = text.splitn(2, pattern).collect();
    if splitter.len() != 2 {
        return Err(anyhow!("Invalid split of '{}' on '{}'.", text, pattern));
    }
    let first = splitter[0];
    let second = splitter[1];
    Ok((first.to_string(), second.to_string()))
}

/// A utility function to dump the contents of a file to the terminal.
pub fn dump_log_to_out<W: Write>(log: &PathBuf, out: &mut W) -> Result<()> {
    let mut out_handle = std::io::BufWriter::new(out);
    let mut buf = Vec::with_capacity(64);
    let mut buf_reader = std::io::BufReader::new(File::open(log)?);
    while let Ok(len) = buf_reader.read_until(b'\n', &mut buf) {
        if len == 0 {
            break;
        }
        out_handle.write_all(&buf)?;
        buf.clear();
        out_handle.flush()?;
    }
    Ok(())
}

/// Returns the local network interface that is up, supports
/// multicast, has an IPv4 address, and is not the loopback interface.
pub fn get_local_network_interface() -> Result<Option<InetAddr>> {
    Ok(getifaddrs()
        .context("reading network interface information")?
        .filter(|addr| {
            addr.flags.contains(InterfaceFlags::IFF_UP)
                && addr.flags.contains(InterfaceFlags::IFF_MULTICAST)
                && !addr.flags.contains(InterfaceFlags::IFF_LOOPBACK)
        })
        .filter_map(
            |addr| {
                if let Some(SockAddr::Inet(inet)) = addr.address {
                    Some(inet)
                } else {
                    None
                }
            },
        )
        .find(|inet| matches!(inet, &InetAddr::V4(_))))
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_split_once() {
        // Can't split an empty string.
        assert!(split_once("", " ").is_err());

        // Can't split on a character that isn't in the text.
        assert!(split_once("something", " ").is_err());

        // Splitting on an empty pattern returns ("", text).
        // This is strange, but expected and documented.
        let result = split_once("something", "");
        assert!(result.is_ok());
        assert_eq!(result.unwrap(), ("".to_string(), "something".to_string()));

        // This also results in a successful ("", "") when they're both empty.
        let result = split_once("", "");
        assert!(result.is_ok());
        assert_eq!(result.unwrap(), ("".to_string(), "".to_string()));

        // A simple split on a space.
        let result = split_once("A B", " ");
        assert!(result.is_ok());
        assert_eq!(result.unwrap(), ("A".to_string(), "B".to_string()));

        // A simple split on a colon.
        let result = split_once("A:B", ":");
        assert!(result.is_ok());
        assert_eq!(result.unwrap(), ("A".to_string(), "B".to_string()));

        // Splitting when there are multiple separators returns (first, remainder).
        let result = split_once("A:B:C:D:E", ":");
        assert!(result.is_ok());
        assert_eq!(result.unwrap(), ("A".to_string(), "B:C:D:E".to_string()));

        // Splitting on a more complex pattern.
        let result = split_once("A pattern can be just about anything", "pattern");
        assert!(result.is_ok());
        assert_eq!(result.unwrap(), ("A ".to_string(), " can be just about anything".to_string()));

        // When the pattern is the first thing in the text.
        let result = split_once("A pattern", "pattern");
        assert!(result.is_ok());
        assert_eq!(result.unwrap(), ("A ".to_string(), "".to_string()));

        // When the pattern is the last thing in the text.
        let result = split_once("pattern B", "pattern");
        assert!(result.is_ok());
        assert_eq!(result.unwrap(), ("".to_string(), " B".to_string()));

        // When the pattern is the only thing in the text.
        let result = split_once("pattern", "pattern");
        assert!(result.is_ok());
        assert_eq!(result.unwrap(), ("".to_string(), "".to_string()));
    }
}
