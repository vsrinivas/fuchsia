// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use ansi_term::Color;
use anyhow::Error;
use regex::Regex;
use std::io::{self, BufRead};

struct LineFormatter {
    time_regex: Regex,
    tag_level_regex: Regex,
    filename_regex: Regex,
}

impl LineFormatter {
    fn new() -> Self {
        // This regex takes a raw serial log, discards the PID/TID and extracts the time.
        let time_regex =
            Regex::new(r"^\[(?P<time>\d+\.\d+)\] \d+:\d+> (?P<remaining>.+)$").unwrap();

        // This regex takes the remainder of the log and extracts the tags and level.
        let tag_level_regex = Regex::new(
            r"^\[(?P<tags>.*)\] (?P<level>TRACE|DEBUG|INFO|WARNING|ERROR): (?P<remaining>.+)$",
        )
        .unwrap();

        // This regex takes the remainder of the log and discards the filename.
        // This regex is brittle because it expects files to be one of three types
        // (.go, .cc or .rs) and follow particular output formats.
        let filename_regex =
            Regex::new(r"^\S+(\.go|\.cc|\.rs)\(\d+\)[:\]]\s?(?P<remaining>.+)$").unwrap();

        Self { time_regex, tag_level_regex, filename_regex }
    }

    fn format(&self, line: String) -> String {
        if let Some(captures) = self.time_regex.captures(line.as_str()) {
            // We know the monotonic time of this message
            let time = captures.name("time").unwrap();
            let time = time.as_str().parse::<f64>().unwrap();

            let remaining = captures.name("remaining").unwrap();
            let remaining = remaining.as_str();

            if let Some(captures) = self.tag_level_regex.captures(remaining) {
                // We know the tags + log level in this message
                let tags = captures.name("tags").unwrap();
                let tags = tags.as_str();

                // Map the common log levels to character-color pairs.
                // If color is None, then terminal default is presumed.
                let level = captures.name("level").unwrap();
                let level_color = match level.as_str() {
                    "TRACE" => ("T", Some(Color::Cyan)),
                    "DEBUG" => ("D", Some(Color::Purple)),
                    "INFO" => ("I", None),
                    "WARNING" => ("W", Some(Color::Yellow)),
                    "ERROR" => ("E", Some(Color::Red)),
                    l => (l, None),
                };

                let remaining = captures.name("remaining").unwrap();
                let remaining = remaining.as_str();

                // Try to remove the filename from the remaining log message
                let remaining = if let Some(captures) = self.filename_regex.captures(remaining) {
                    // The filename was detected and removed
                    let remaining = captures.name("remaining").unwrap();
                    remaining.as_str()
                } else {
                    // The filename could not be found
                    remaining
                };

                let (level, color) = level_color;
                let line = format!("[{:.3}][{}][{}] {}", time, tags, level, remaining);

                if let Some(color) = color {
                    // Color code the line
                    return color.paint(line).to_string();
                } else {
                    return line;
                }
            } else {
                // Print just the time + remaining message (that's all we know)
                return format!("[{:.3}] {}", time, remaining);
            }
        } else {
            // This line didn't match any of our expected formats.
            // Print it as is.
            return line;
        }
    }
}

fn main() -> Result<(), Error> {
    println!("Pretty Serial is running!");
    let f = LineFormatter::new();

    // Assume that serial logs are piped in via stdin.
    let stdin = io::stdin();
    let handle = stdin.lock();

    // Process the logs line-by-line
    for line in handle.lines() {
        if let Ok(line) = line {
            println!("{}", f.format(line));
        }
    }

    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    pub fn format_cases() {
        let cases = vec![
            (
                "[01069.017] 24371:00000> [netstack, DHCP] WARNING: client.go(676): ethx7a: recv timeout waiting for dhcpOFFER; retransmitting dhcpDISCOVER",
                Color::Yellow.paint("[1069.017][netstack, DHCP][W] ethx7a: recv timeout waiting for dhcpOFFER; retransmitting dhcpDISCOVER").to_string()
            ),
            (
                "[00002.185] 01728:01731> netsvc: using /dev/class/ethernet/000",
                "[2.185] netsvc: using /dev/class/ethernet/000".to_string()
            ),
            (
                "[00020.833] 23092:23094> [netcfg] INFO: discovered host interface with id=2, configuring interface",
                "[20.833][netcfg][I] discovered host interface with id=2, configuring interface".to_string()
            ),
            (
                "$ [00000.619] 00000:01025> CPU  0:  0,  3,  2,  1",
                "$ [00000.619] 00000:01025> CPU  0:  0,  3,  2,  1".to_string()
            )
        ];

        let f = LineFormatter::new();
        for (input, expected) in cases {
            let actual = f.format(input.to_string());
            assert_eq!(actual, expected);
        }
    }
}
