// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! This library contains the shared functions used by multiple emulation engines. Any code placed
//! in this library may not depend on any other code within the plugin, with the exception of "args"
//! libraries.

use anyhow::{anyhow, Result};
use std::process::Command;

// Provides access to ffx_config properties.
pub mod config;
pub mod process;
pub mod target;

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

/// A utility function for testing if a Tap interface is up and available. Assumes the existence
/// of the "ip" program for finding the interface, which is usually preinstalled on Linux hosts
/// but not MacOS hosts. Conservatively assumes any error indicates Tap is unavailable.
pub fn tap_available() -> bool {
    if std::env::consts::OS != "linux" {
        return false;
    }
    let output = Command::new("ip").args(["tuntap", "show"]).output();
    // Output contains the interface name, if it exists. Tap is considered available if the call
    // succeeds and output is non-empty.
    // TODO(fxbug.dev/87464): Check for the expected interface name, to make sure we have the right
    // one if there are multiple taps possible (i.e. for supporting multiple emu instances).
    return output.is_ok() && output.unwrap().stdout.len() > 0;
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
