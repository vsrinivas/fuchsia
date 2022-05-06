// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Argument-parsing specification for the `signal` subcommand.

use {argh::FromArgs, ffx_core::ffx_command, fidl_fuchsia_memorypressure::Level};

/// Signals userspace clients with specified memory pressure
/// level. Clients can use this command to test their response to
/// memory pressure. Does not affect the real memory pressure level on
/// the system, or trigger any kernel reclamation tasks.
#[ffx_command()]
#[derive(FromArgs, Debug, PartialEq)]
#[argh(subcommand, name = "signal")]
pub struct SignalCommand {
    /// memory pressure level. Can be CRITICAL, WARNING or NORMAL.
    #[argh(positional, from_str_fn(parse_memory_pressure_level))]
    pub level: Level,
}

/// Parses memory pressure level. Accepted values are CRITICAL,
/// WARNING and NORMAL.
fn parse_memory_pressure_level(value: &str) -> Result<Level, String> {
    match value {
        "NORMAL" => Ok(Level::Normal),
        "WARNING" => Ok(Level::Warning),
        "CRITICAL" => Ok(Level::Critical),
        _ => Err(format!("Unrecognized level: {value}")),
    }
}

#[cfg(test)]
mod tests {
    use {super::*, assert_matches::assert_matches};

    #[test]
    fn can_parse_canonical_levels() {
        assert_eq!(parse_memory_pressure_level("NORMAL"), Ok(Level::Normal));
        assert_eq!(parse_memory_pressure_level("WARNING"), Ok(Level::Warning));
        assert_eq!(parse_memory_pressure_level("CRITICAL"), Ok(Level::Critical));
    }

    #[test]
    fn cannot_parse_lowercase_levels() {
        assert_matches!(parse_memory_pressure_level("normal"), Err(_));
        assert_matches!(parse_memory_pressure_level("warning"), Err(_));
        assert_matches!(parse_memory_pressure_level("critical"), Err(_));
    }

    #[test]
    fn cannot_parse_capitalized_levels() {
        assert_matches!(parse_memory_pressure_level("Normal"), Err(_));
        assert_matches!(parse_memory_pressure_level("Warning"), Err(_));
        assert_matches!(parse_memory_pressure_level("Critical"), Err(_));
    }

    #[test]
    fn cannot_parse_nonsensical_levels() {
        assert_matches!(parse_memory_pressure_level("Nomral"), Err(_));
        assert_matches!(parse_memory_pressure_level("level"), Err(_));
        assert_matches!(parse_memory_pressure_level(""), Err(_));
        assert_matches!(parse_memory_pressure_level("     "), Err(_));
        assert_matches!(parse_memory_pressure_level(" Normal "), Err(_));
    }
}
