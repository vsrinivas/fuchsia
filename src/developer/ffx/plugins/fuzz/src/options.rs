// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{anyhow, bail, Error, Result},
    fidl_fuchsia_fuzzer as fuzz,
    fuchsia_fuzzctl::constants::*,
    lazy_static::lazy_static,
    regex::Regex,
};

/// Names of allowable fuzzer options corresponding to `fuchsia.fuzzer.Options`.
pub const NAMES: &[&str] = &[
    "runs",
    "max_total_time",
    "seed",
    "max_input_size",
    "mutation_depth",
    "dictionary_level",
    "detect_exits",
    "detect_leaks",
    "run_limit",
    "malloc_limit",
    "oom_limit",
    "purge_interval",
    "malloc_exitcode",
    "death_exitcode",
    "leak_exitcode",
    "oom_exitcode",
    "pulse_interval",
    "debug",
    "print_final_stats",
    "use_value_profile",
    "asan_options",
    "ubsan_options",
];

/// Returns a field of `options` based on its name.
pub fn get(options: &fuzz::Options, name: &str) -> Result<String> {
    match name {
        "runs" => Ok(options.runs.unwrap().to_string()),
        "max_total_time" => Ok(format_duration(options.max_total_time)),
        "seed" => Ok(options.seed.unwrap().to_string()),
        "max_input_size" => Ok(format_size(options.max_input_size)),
        "mutation_depth" => Ok(options.mutation_depth.unwrap().to_string()),
        "dictionary_level" => Ok(options.dictionary_level.unwrap().to_string()),
        "detect_exits" => Ok(options.detect_exits.unwrap().to_string()),
        "detect_leaks" => Ok(options.detect_leaks.unwrap().to_string()),
        "run_limit" => Ok(format_duration(options.run_limit)),
        "malloc_limit" => Ok(format_size(options.malloc_limit)),
        "oom_limit" => Ok(format_size(options.oom_limit)),
        "purge_interval" => Ok(format_duration(options.purge_interval)),
        "malloc_exitcode" => Ok(options.malloc_exitcode.unwrap().to_string()),
        "death_exitcode" => Ok(options.death_exitcode.unwrap().to_string()),
        "leak_exitcode" => Ok(options.leak_exitcode.unwrap().to_string()),
        "oom_exitcode" => Ok(options.oom_exitcode.unwrap().to_string()),
        "pulse_interval" => Ok(format_duration(options.pulse_interval)),
        "debug" => Ok(options.debug.unwrap().to_string()),
        "print_final_stats" => Ok(options.print_final_stats.unwrap().to_string()),
        "use_value_profile" => Ok(options.use_value_profile.unwrap().to_string()),
        "asan_options" => {
            let value = match &options.sanitizer_options {
                Some(fuzz::SanitizerOptions { name, value }) if name == "ASAN_OPTIONS" => value,
                _ => "",
            };
            Ok(format!("\"{}\"", value))
        }
        "ubsan_options" => {
            let value = match &options.sanitizer_options {
                Some(fuzz::SanitizerOptions { name, value }) if name == "UBSAN_OPTIONS" => value,
                _ => "",
            };
            Ok(format!("\"{}\"", value))
        }
        _ => Err(anyhow!("unrecognized option: {}", name)),
    }
}

/// Returns name/value pairs for all fields in `options`.
pub fn get_all(options: &fuzz::Options) -> Vec<(String, String)> {
    NAMES.iter().cloned().map(|name| (name.to_string(), get(options, name).unwrap())).collect()
}

/// Parses the `name` and `value` and sets the corresponding field in `options`.
pub fn set(options: &mut fuzz::Options, name: &str, value: &str) -> Result<()> {
    match name {
        "runs" => {
            let runs = value.parse::<u32>().map_err(Error::msg)?;
            options.runs = Some(runs);
        }
        "max_total_time" => {
            let max_total_time = parse_duration(value).map_err(Error::msg)?;
            options.max_total_time = Some(max_total_time);
        }
        "seed" => {
            let seed = value.parse::<u32>().map_err(Error::msg)?;
            options.seed = Some(seed);
        }
        "max_input_size" => {
            let max_input_size = parse_size(value).map_err(Error::msg)?;
            options.max_input_size = Some(max_input_size);
        }
        "mutation_depth" => {
            let mutation_depth = value.parse::<u16>().map_err(Error::msg)?;
            options.mutation_depth = Some(mutation_depth);
        }
        "dictionary_level" => {
            let dictionary_level = value.parse::<u16>().map_err(Error::msg)?;
            options.dictionary_level = Some(dictionary_level);
        }
        "detect_exits" => {
            let detect_exits = parse_bool(value)?;
            options.detect_exits = Some(detect_exits);
        }
        "detect_leaks" => {
            let detect_leaks = parse_bool(value)?;
            options.detect_leaks = Some(detect_leaks);
        }
        "run_limit" => {
            let run_limit = parse_duration(value).map_err(Error::msg)?;
            options.run_limit = Some(run_limit);
        }
        "malloc_limit" => {
            let malloc_limit = parse_size(value).map_err(Error::msg)?;
            options.malloc_limit = Some(malloc_limit);
        }
        "oom_limit" => {
            let oom_limit = parse_size(value).map_err(Error::msg)?;
            options.oom_limit = Some(oom_limit);
        }
        "purge_interval" => {
            let purge_interval = parse_duration(value).map_err(Error::msg)?;
            options.purge_interval = Some(purge_interval);
        }
        "malloc_exitcode" => {
            let malloc_exitcode = value.parse::<i32>().map_err(Error::msg)?;
            options.malloc_exitcode = Some(malloc_exitcode);
        }
        "death_exitcode" => {
            let death_exitcode = value.parse::<i32>().map_err(Error::msg)?;
            options.death_exitcode = Some(death_exitcode);
        }
        "leak_exitcode" => {
            let leak_exitcode = value.parse::<i32>().map_err(Error::msg)?;
            options.leak_exitcode = Some(leak_exitcode);
        }
        "oom_exitcode" => {
            let oom_exitcode = value.parse::<i32>().map_err(Error::msg)?;
            options.oom_exitcode = Some(oom_exitcode);
        }
        "pulse_interval" => {
            let pulse_interval = parse_duration(value).map_err(Error::msg)?;
            options.pulse_interval = Some(pulse_interval);
        }
        "debug" => {
            let debug = parse_bool(value)?;
            options.debug = Some(debug);
        }
        "print_final_stats" => {
            let print_final_stats = parse_bool(value)?;
            options.print_final_stats = Some(print_final_stats);
        }
        "use_value_profile" => {
            let use_value_profile = parse_bool(value)?;
            options.use_value_profile = Some(use_value_profile);
        }
        "asan_options" => {
            options.sanitizer_options = Some(fuzz::SanitizerOptions {
                name: "ASAN_OPTIONS".to_string(),
                value: value.to_string(),
            })
        }
        "ubsan_options" => {
            options.sanitizer_options = Some(fuzz::SanitizerOptions {
                name: "UBSAN_OPTIONS".to_string(),
                value: value.to_string(),
            })
        }
        _ => bail!("unrecognized option: {}", name),
    }
    Ok(())
}

fn strip_quotes(value: &str) -> String {
    let mut value = value.to_string();
    if value.chars().next() == Some('"') && value.chars().last() == Some('"') {
        let n = value.chars().count() - 1;
        if 1 < n {
            value = value[1..n].to_string();
        }
    }
    value
}

// `value.parse::<bool>()` requires value to be "true" or "false", but libFuzzer and ClusterFuzz
// prefer to use 1 and 0, respectively.
fn parse_bool(value: &str) -> Result<bool> {
    if let Ok(uval) = value.parse::<u64>() {
        return Ok(uval > 0);
    }
    value.parse::<bool>().map_err(Error::msg)
}

fn format_duration(nanos: Option<i64>) -> String {
    match nanos.unwrap() {
        0 => "0".to_string(),
        nanos if nanos % NANOS_PER_DAY == 0 => format!("\"{}d\"", nanos / NANOS_PER_DAY),
        nanos if nanos % NANOS_PER_HOUR == 0 => format!("\"{}h\"", nanos / NANOS_PER_HOUR),
        nanos if nanos % NANOS_PER_MINUTE == 0 => format!("\"{}m\"", nanos / NANOS_PER_MINUTE),
        nanos if nanos % NANOS_PER_SECOND == 0 => format!("\"{}s\"", nanos / NANOS_PER_SECOND),
        nanos if nanos % NANOS_PER_MILLI == 0 => format!("\"{}ms\"", nanos / NANOS_PER_MILLI),
        nanos if nanos % NANOS_PER_MICRO == 0 => format!("\"{}us\"", nanos / NANOS_PER_MICRO),
        nanos => format!("\"{}ns\"", nanos),
    }
}

fn parse_duration(value: &str) -> Result<i64> {
    lazy_static! {
        static ref NANOS_RE: Regex = Regex::new("^(\\d+(?:\\.\\d+)?)ns$").unwrap();
        static ref MICROS_RE: Regex = Regex::new("^(\\d+(?:\\.\\d+)?)us$").unwrap();
        static ref MILLIS_RE: Regex = Regex::new("^(\\d+(?:\\.\\d+)?)ms$").unwrap();
        static ref SECONDS_RE: Regex = Regex::new("^(\\d+(?:\\.\\d+)?)s?$").unwrap();
        static ref MINUTES_RE: Regex = Regex::new("^(\\d+(?:\\.\\d+)?)m$").unwrap();
        static ref HOURS_RE: Regex = Regex::new("^(\\d+(?:\\.\\d+)?)h$").unwrap();
        static ref DAYS_RE: Regex = Regex::new("^(\\d+(?:\\.\\d+)?)d$").unwrap();
    }
    let value = strip_quotes(value);
    if let Some(captures) = NANOS_RE.captures(&value) {
        let ns = captures[1].parse::<f64>().map_err(Error::msg)?;
        return Ok(ns as i64);
    }
    if let Some(captures) = MICROS_RE.captures(&value) {
        let us = captures[1].parse::<f64>().map_err(Error::msg)?;
        return Ok((us * NANOS_PER_MICRO as f64) as i64);
    }
    if let Some(captures) = MILLIS_RE.captures(&value) {
        let ms = captures[1].parse::<f64>().map_err(Error::msg)?;
        return Ok((ms * NANOS_PER_MILLI as f64) as i64);
    }
    if let Some(captures) = SECONDS_RE.captures(&value) {
        let s = captures[1].parse::<f64>().map_err(Error::msg)?;
        return Ok((s * NANOS_PER_SECOND as f64) as i64);
    }
    if let Some(captures) = MINUTES_RE.captures(&value) {
        let m = captures[1].parse::<f64>().map_err(Error::msg)?;
        return Ok((m * NANOS_PER_MINUTE as f64) as i64);
    }
    if let Some(captures) = HOURS_RE.captures(&value) {
        let h = captures[1].parse::<f64>().map_err(Error::msg)?;
        return Ok((h * NANOS_PER_HOUR as f64) as i64);
    }
    if let Some(captures) = DAYS_RE.captures(&value) {
        let d = captures[1].parse::<f64>().map_err(Error::msg)?;
        return Ok((d * NANOS_PER_DAY as f64) as i64);
    }
    bail!("failed to parse duration: {}", value);
}

fn format_size(bytes: Option<u64>) -> String {
    match bytes.unwrap() {
        bytes if bytes == 0 => "0".to_string(),
        bytes if bytes % BYTES_PER_GB == 0 => format!("\"{}gb\"", bytes / BYTES_PER_GB),
        bytes if bytes % BYTES_PER_MB == 0 => format!("\"{}mb\"", bytes / BYTES_PER_MB),
        bytes if bytes % BYTES_PER_KB == 0 => format!("\"{}kb\"", bytes / BYTES_PER_KB),
        bytes => format!("\"{}b\"", bytes),
    }
}

fn parse_size(value: &str) -> Result<u64> {
    lazy_static! {
        static ref B_RE: Regex = Regex::new("^(\\d+(\\.\\d+)?)b$").unwrap();
        static ref KB_RE: Regex = Regex::new("^(\\d+(\\.\\d+)?)kb$").unwrap();
        static ref MB_RE: Regex = Regex::new("^(\\d+(\\.\\d+)?)(mb)?$").unwrap();
        static ref GB_RE: Regex = Regex::new("^(\\d+(\\.\\d+)?)gb$").unwrap();
    }
    let value = strip_quotes(value);
    if let Some(captures) = B_RE.captures(&value) {
        let b = captures[1].parse::<f64>().map_err(Error::msg)?;
        return Ok(b as u64);
    }
    if let Some(captures) = KB_RE.captures(&value) {
        let kb = captures[1].parse::<f64>().map_err(Error::msg)?;
        return Ok((kb * BYTES_PER_KB as f64) as u64);
    }
    if let Some(captures) = MB_RE.captures(&value) {
        let mb = captures[1].parse::<f64>().map_err(Error::msg)?;
        return Ok((mb * BYTES_PER_MB as f64) as u64);
    }
    if let Some(captures) = GB_RE.captures(&value) {
        let gb = captures[1].parse::<f64>().map_err(Error::msg)?;
        return Ok((gb * BYTES_PER_GB as f64) as u64);
    }
    bail!("failed to parse size: {}", value);
}

#[cfg(test)]
mod tests {
    use super::*;
    use fuchsia_fuzzctl_test::add_defaults;

    #[test]
    fn test_format_duration() -> Result<()> {
        assert_eq!(format_duration(Some(1)), "\"1ns\"".to_string());
        assert_eq!(format_duration(Some(12 * NANOS_PER_MICRO)), "\"12us\"".to_string());
        assert_eq!(format_duration(Some(123 * NANOS_PER_MILLI)), "\"123ms\"".to_string());
        assert_eq!(format_duration(Some(1234 * NANOS_PER_SECOND)), "\"1234s\"".to_string());
        assert_eq!(format_duration(Some(20 * NANOS_PER_MINUTE)), "\"20m\"".to_string());
        assert_eq!(format_duration(Some(2 * NANOS_PER_HOUR)), "\"2h\"".to_string());
        assert_eq!(format_duration(Some(1 * NANOS_PER_DAY)), "\"1d\"".to_string());
        Ok(())
    }

    #[test]
    fn test_parse_duration() -> Result<()> {
        // Error doesn't implement Eq.
        assert_eq!(parse_duration("0").ok(), Some(0));
        assert_eq!(parse_duration("\"0\"").ok(), Some(0));
        assert_eq!(parse_duration("1").ok(), Some(1 * NANOS_PER_SECOND));
        assert_eq!(parse_duration("2ns").ok(), Some(2));
        assert_eq!(parse_duration("3us").ok(), Some(3 * NANOS_PER_MICRO));
        assert_eq!(parse_duration("4ms").ok(), Some(4 * NANOS_PER_MILLI));
        assert_eq!(parse_duration("5s").ok(), Some(5 * NANOS_PER_SECOND));
        assert_eq!(parse_duration("6m").ok(), Some(6 * NANOS_PER_MINUTE));
        assert_eq!(parse_duration("7h").ok(), Some(7 * NANOS_PER_HOUR));
        assert_eq!(parse_duration("8d").ok(), Some(8 * NANOS_PER_DAY));
        assert_eq!(parse_duration("\"8d\"").ok(), Some(8 * NANOS_PER_DAY));
        assert!(parse_duration("\"").is_err());
        assert!(parse_duration("9w").is_err());
        assert!(parse_duration("-10s").is_err());
        assert!(parse_duration("-11").is_err());
        assert!(parse_duration("").is_err());
        assert_eq!(parse_duration("2.5d").ok(), Some(60 * NANOS_PER_HOUR));
        assert_eq!(parse_duration("0.25h").ok(), Some(15 * NANOS_PER_MINUTE));
        assert_eq!(parse_duration("\"0.025\"").ok(), Some(25 * NANOS_PER_MILLI));
        Ok(())
    }

    #[test]
    fn test_format_size() -> Result<()> {
        assert_eq!(format_size(Some(0)), "0".to_string());
        assert_eq!(format_size(Some(1)), "\"1b\"".to_string());
        assert_eq!(format_size(Some(2 * BYTES_PER_KB)), "\"2kb\"".to_string());
        assert_eq!(format_size(Some(3 * BYTES_PER_MB)), "\"3mb\"".to_string());
        assert_eq!(format_size(Some(4 * BYTES_PER_GB)), "\"4gb\"".to_string());
        Ok(())
    }

    #[test]
    fn test_parse_size() -> Result<()> {
        // Error doesn't implement Eq.
        assert_eq!(parse_size("0").ok(), Some(0));
        assert_eq!(parse_size("\"0\"").ok(), Some(0));
        assert_eq!(parse_size("1").ok(), Some(1 * BYTES_PER_MB));
        assert_eq!(parse_size("2b").ok(), Some(2));
        assert_eq!(parse_size("33kb").ok(), Some(33 * BYTES_PER_KB));
        assert_eq!(parse_size("444mb").ok(), Some(444 * BYTES_PER_MB));
        assert_eq!(parse_size("5555gb").ok(), Some(5555 * BYTES_PER_GB));
        assert_eq!(parse_size("\"5555gb\"").ok(), Some(5555 * BYTES_PER_GB));
        assert!(parse_size("\"").is_err());
        assert!(parse_size("6tb").is_err());
        assert!(parse_size("-7mb").is_err());
        assert!(parse_size("-8").is_err());
        assert!(parse_size("").is_err());
        assert_eq!(parse_size("6.25kb").ok(), Some(6400));
        assert_eq!(parse_size("\"0.0625\"").ok(), Some(65536));
        Ok(())
    }

    #[test]
    fn test_runs() -> Result<()> {
        let mut fuzz_options = fuzz::Options::EMPTY;
        assert!(set(&mut fuzz_options, "runs", "0").is_ok());
        assert_eq!(fuzz_options.runs, Some(0));
        assert_eq!(get(&fuzz_options, "runs").unwrap(), "0");
        assert!(set(&mut fuzz_options, "runs", "1").is_ok());
        assert_eq!(fuzz_options.runs, Some(1));
        assert_eq!(get(&fuzz_options, "runs").unwrap(), "1");
        assert!(set(&mut fuzz_options, "runs", "-1").is_err());
        assert!(set(&mut fuzz_options, "runs", "one").is_err());
        Ok(())
    }

    #[test]
    fn test_max_total_time() -> Result<()> {
        let mut fuzz_options = fuzz::Options::EMPTY;
        assert!(set(&mut fuzz_options, "max_total_time", "0").is_ok());
        assert_eq!(fuzz_options.max_total_time, Some(0));
        assert_eq!(get(&fuzz_options, "max_total_time").unwrap(), "0");

        assert!(set(&mut fuzz_options, "max_total_time", "1h").is_ok());
        assert_eq!(fuzz_options.max_total_time, Some(1 * NANOS_PER_HOUR));
        assert_eq!(get(&fuzz_options, "max_total_time").unwrap(), "\"1h\"");

        assert!(set(&mut fuzz_options, "max_total_time", "0.1d").is_ok());
        assert_eq!(fuzz_options.max_total_time, Some(144 * NANOS_PER_MINUTE));
        assert_eq!(get(&fuzz_options, "max_total_time").unwrap(), "\"144m\"");

        assert!(set(&mut fuzz_options, "max_total_time", "-1h").is_err());
        assert!(set(&mut fuzz_options, "max_total_time", "forever").is_err());
        Ok(())
    }

    #[test]
    fn test_seed() -> Result<()> {
        let mut fuzz_options = fuzz::Options::EMPTY;
        assert!(set(&mut fuzz_options, "seed", "0").is_ok());
        assert_eq!(fuzz_options.seed, Some(0));
        assert_eq!(get(&fuzz_options, "seed").unwrap(), "0");
        assert!(set(&mut fuzz_options, "seed", "1337").is_ok());
        assert_eq!(fuzz_options.seed, Some(1337));
        assert_eq!(get(&fuzz_options, "seed").unwrap(), "1337");
        assert!(set(&mut fuzz_options, "seed", "-2").is_err());
        assert!(set(&mut fuzz_options, "seed", "sunflower").is_err());
        Ok(())
    }

    #[test]
    fn test_max_input_size() -> Result<()> {
        let mut fuzz_options = fuzz::Options::EMPTY;
        assert!(set(&mut fuzz_options, "max_input_size", "0").is_ok());
        assert_eq!(fuzz_options.max_input_size, Some(0));
        assert_eq!(get(&fuzz_options, "max_input_size").unwrap(), "0");

        assert!(set(&mut fuzz_options, "max_input_size", "2mb").is_ok());
        assert_eq!(fuzz_options.max_input_size, Some(2 * BYTES_PER_MB));
        assert_eq!(get(&fuzz_options, "max_input_size").unwrap(), "\"2mb\"");

        assert!(set(&mut fuzz_options, "max_input_size", "0.5gb").is_ok());
        assert_eq!(fuzz_options.max_input_size, Some(512 * BYTES_PER_MB));
        assert_eq!(get(&fuzz_options, "max_input_size").unwrap(), "\"512mb\"");

        assert!(set(&mut fuzz_options, "max_input_size", "-3").is_err());
        assert!(set(&mut fuzz_options, "max_input_size", "big").is_err());
        Ok(())
    }

    #[test]
    fn test_mutation_depth() -> Result<()> {
        let mut fuzz_options = fuzz::Options::EMPTY;
        assert!(set(&mut fuzz_options, "mutation_depth", "0").is_ok());
        assert_eq!(fuzz_options.mutation_depth, Some(0));
        assert_eq!(get(&fuzz_options, "mutation_depth").unwrap(), "0");

        assert!(set(&mut fuzz_options, "mutation_depth", "10").is_ok());
        assert_eq!(fuzz_options.mutation_depth, Some(10));
        assert_eq!(get(&fuzz_options, "mutation_depth").unwrap(), "10");

        assert!(set(&mut fuzz_options, "mutation_depth", "-4").is_err());
        assert!(set(&mut fuzz_options, "mutation_depth", "ninja-turtle").is_err());
        Ok(())
    }

    #[test]
    fn test_dictionary_level() -> Result<()> {
        let mut fuzz_options = fuzz::Options::EMPTY;
        assert!(set(&mut fuzz_options, "dictionary_level", "0").is_ok());
        assert_eq!(fuzz_options.dictionary_level, Some(0));
        assert_eq!(get(&fuzz_options, "dictionary_level").unwrap(), "0");

        assert!(set(&mut fuzz_options, "dictionary_level", "9").is_ok());
        assert_eq!(fuzz_options.dictionary_level, Some(9));
        assert_eq!(get(&fuzz_options, "dictionary_level").unwrap(), "9");

        assert!(set(&mut fuzz_options, "dictionary_level", "-5").is_err());
        assert!(set(&mut fuzz_options, "dictionary_level", "zed").is_err());
        Ok(())
    }

    #[test]
    fn test_detect_exits() -> Result<()> {
        let mut fuzz_options = fuzz::Options::EMPTY;
        assert!(set(&mut fuzz_options, "detect_exits", "false").is_ok());
        assert_eq!(fuzz_options.detect_exits, Some(false));
        assert_eq!(get(&fuzz_options, "detect_exits").unwrap(), "false");

        assert!(set(&mut fuzz_options, "detect_exits", "0").is_ok());
        assert_eq!(fuzz_options.detect_exits, Some(false));
        assert_eq!(get(&fuzz_options, "detect_exits").unwrap(), "false");

        assert!(set(&mut fuzz_options, "detect_exits", "true").is_ok());
        assert_eq!(fuzz_options.detect_exits, Some(true));
        assert_eq!(get(&fuzz_options, "detect_exits").unwrap(), "true");

        assert!(set(&mut fuzz_options, "detect_exits", "1").is_ok());
        assert_eq!(fuzz_options.detect_exits, Some(true));
        assert_eq!(get(&fuzz_options, "detect_exits").unwrap(), "true");

        assert!(set(&mut fuzz_options, "detect_exits", "-1").is_err());
        assert!(set(&mut fuzz_options, "detect_exits", "maybe").is_err());
        Ok(())
    }

    #[test]
    fn test_detect_leaks() -> Result<()> {
        let mut fuzz_options = fuzz::Options::EMPTY;
        assert!(set(&mut fuzz_options, "detect_leaks", "false").is_ok());
        assert_eq!(fuzz_options.detect_leaks, Some(false));
        assert_eq!(get(&fuzz_options, "detect_leaks").unwrap(), "false");

        assert!(set(&mut fuzz_options, "detect_leaks", "0").is_ok());
        assert_eq!(fuzz_options.detect_leaks, Some(false));
        assert_eq!(get(&fuzz_options, "detect_leaks").unwrap(), "false");

        assert!(set(&mut fuzz_options, "detect_leaks", "true").is_ok());
        assert_eq!(fuzz_options.detect_leaks, Some(true));
        assert_eq!(get(&fuzz_options, "detect_leaks").unwrap(), "true");

        assert!(set(&mut fuzz_options, "detect_leaks", "1").is_ok());
        assert_eq!(fuzz_options.detect_leaks, Some(true));
        assert_eq!(get(&fuzz_options, "detect_leaks").unwrap(), "true");

        assert!(set(&mut fuzz_options, "detect_leaks", "-1").is_err());
        assert!(set(&mut fuzz_options, "detect_leaks", "maybe").is_err());
        Ok(())
    }

    #[test]
    fn test_run_limit() -> Result<()> {
        let mut fuzz_options = fuzz::Options::EMPTY;
        assert!(set(&mut fuzz_options, "run_limit", "0").is_ok());
        assert_eq!(fuzz_options.run_limit, Some(0));
        assert_eq!(get(&fuzz_options, "run_limit").unwrap(), "0");

        assert!(set(&mut fuzz_options, "run_limit", "10s").is_ok());
        assert_eq!(fuzz_options.run_limit, Some(10 * NANOS_PER_SECOND));
        assert_eq!(get(&fuzz_options, "run_limit").unwrap(), "\"10s\"");

        assert!(set(&mut fuzz_options, "run_limit", "1.5m").is_ok());
        assert_eq!(fuzz_options.run_limit, Some(90 * NANOS_PER_SECOND));
        assert_eq!(get(&fuzz_options, "run_limit").unwrap(), "\"90s\"");

        assert!(set(&mut fuzz_options, "run_limit", "-1").is_err());
        assert!(set(&mut fuzz_options, "run_limit", "forever").is_err());
        Ok(())
    }

    #[test]
    fn test_malloc_limit() -> Result<()> {
        let mut fuzz_options = fuzz::Options::EMPTY;
        assert!(set(&mut fuzz_options, "malloc_limit", "0").is_ok());
        assert_eq!(fuzz_options.malloc_limit, Some(0));
        assert_eq!(get(&fuzz_options, "malloc_limit").unwrap(), "0");

        assert!(set(&mut fuzz_options, "malloc_limit", "1gb").is_ok());
        assert_eq!(fuzz_options.malloc_limit, Some(1 * BYTES_PER_GB));
        assert_eq!(get(&fuzz_options, "malloc_limit").unwrap(), "\"1gb\"");

        assert!(set(&mut fuzz_options, "malloc_limit", "0.25gb").is_ok());
        assert_eq!(fuzz_options.malloc_limit, Some(256 * BYTES_PER_MB));
        assert_eq!(get(&fuzz_options, "malloc_limit").unwrap(), "\"256mb\"");

        assert!(set(&mut fuzz_options, "malloc_limit", "-1").is_err());
        assert!(set(&mut fuzz_options, "malloc_limit", "1eb").is_err());
        Ok(())
    }

    #[test]
    fn test_oom_limit() -> Result<()> {
        let mut fuzz_options = fuzz::Options::EMPTY;
        assert!(set(&mut fuzz_options, "oom_limit", "0").is_ok());
        assert_eq!(fuzz_options.oom_limit, Some(0));
        assert_eq!(get(&fuzz_options, "oom_limit").unwrap(), "0");

        assert!(set(&mut fuzz_options, "oom_limit", "1gb").is_ok());
        assert_eq!(fuzz_options.oom_limit, Some(1 * BYTES_PER_GB));
        assert_eq!(get(&fuzz_options, "oom_limit").unwrap(), "\"1gb\"");

        assert!(set(&mut fuzz_options, "oom_limit", "1.5gb").is_ok());
        assert_eq!(fuzz_options.oom_limit, Some(1536 * BYTES_PER_MB));
        assert_eq!(get(&fuzz_options, "oom_limit").unwrap(), "\"1536mb\"");

        assert!(set(&mut fuzz_options, "oom_limit", "-1").is_err());
        assert!(set(&mut fuzz_options, "oom_limit", "1eb").is_err());
        Ok(())
    }

    #[test]
    fn test_purge_interval() -> Result<()> {
        let mut fuzz_options = fuzz::Options::EMPTY;
        assert!(set(&mut fuzz_options, "purge_interval", "0").is_ok());
        assert_eq!(fuzz_options.purge_interval, Some(0));
        assert_eq!(get(&fuzz_options, "purge_interval").unwrap(), "0");

        assert!(set(&mut fuzz_options, "purge_interval", "1m").is_ok());
        assert_eq!(fuzz_options.purge_interval, Some(1 * NANOS_PER_MINUTE));
        assert_eq!(get(&fuzz_options, "purge_interval").unwrap(), "\"1m\"");

        assert!(set(&mut fuzz_options, "purge_interval", "0.1h").is_ok());
        assert_eq!(fuzz_options.purge_interval, Some(6 * NANOS_PER_MINUTE));
        assert_eq!(get(&fuzz_options, "purge_interval").unwrap(), "\"6m\"");

        assert!(set(&mut fuzz_options, "purge_interval", "-1").is_err());
        assert!(set(&mut fuzz_options, "purge_interval", "constantly").is_err());
        Ok(())
    }

    #[test]
    fn test_malloc_exitcode() -> Result<()> {
        let mut fuzz_options = fuzz::Options::EMPTY;
        assert!(set(&mut fuzz_options, "malloc_exitcode", "2000").is_ok());
        assert_eq!(fuzz_options.malloc_exitcode, Some(2000));
        assert_eq!(get(&fuzz_options, "malloc_exitcode").unwrap(), "2000");

        assert!(set(&mut fuzz_options, "malloc_exitcode", "-77").is_ok());
        assert_eq!(fuzz_options.malloc_exitcode, Some(-77));
        assert_eq!(get(&fuzz_options, "malloc_exitcode").unwrap(), "-77");

        assert!(set(&mut fuzz_options, "malloc_exitcode", "seventy-seven").is_err());
        Ok(())
    }

    #[test]
    fn test_death_exitcode() -> Result<()> {
        let mut fuzz_options = fuzz::Options::EMPTY;
        assert!(set(&mut fuzz_options, "death_exitcode", "2001").is_ok());
        assert_eq!(fuzz_options.death_exitcode, Some(2001));
        assert_eq!(get(&fuzz_options, "death_exitcode").unwrap(), "2001");

        assert!(set(&mut fuzz_options, "death_exitcode", "-78").is_ok());
        assert_eq!(fuzz_options.death_exitcode, Some(-78));
        assert_eq!(get(&fuzz_options, "death_exitcode").unwrap(), "-78");

        assert!(set(&mut fuzz_options, "death_exitcode", "seventy-eight").is_err());
        Ok(())
    }

    #[test]
    fn test_leak_exitcode() -> Result<()> {
        let mut fuzz_options = fuzz::Options::EMPTY;
        assert!(set(&mut fuzz_options, "leak_exitcode", "2002").is_ok());
        assert_eq!(fuzz_options.leak_exitcode, Some(2002));
        assert_eq!(get(&fuzz_options, "leak_exitcode").unwrap(), "2002");

        assert!(set(&mut fuzz_options, "leak_exitcode", "-79").is_ok());
        assert_eq!(fuzz_options.leak_exitcode, Some(-79));
        assert_eq!(get(&fuzz_options, "leak_exitcode").unwrap(), "-79");

        assert!(set(&mut fuzz_options, "leak_exitcode", "seventy-nine").is_err());
        Ok(())
    }

    #[test]
    fn test_oom_exitcode() -> Result<()> {
        let mut fuzz_options = fuzz::Options::EMPTY;
        assert!(set(&mut fuzz_options, "oom_exitcode", "2003").is_ok());
        assert_eq!(fuzz_options.oom_exitcode, Some(2003));
        assert_eq!(get(&fuzz_options, "oom_exitcode").unwrap(), "2003");

        assert!(set(&mut fuzz_options, "oom_exitcode", "-80").is_ok());
        assert_eq!(fuzz_options.oom_exitcode, Some(-80));
        assert_eq!(get(&fuzz_options, "oom_exitcode").unwrap(), "-80");

        assert!(set(&mut fuzz_options, "oom_exitcode", "eighty").is_err());
        Ok(())
    }

    #[test]
    fn test_pulse_interval() -> Result<()> {
        let mut fuzz_options = fuzz::Options::EMPTY;
        assert!(set(&mut fuzz_options, "pulse_interval", "0").is_ok());
        assert_eq!(fuzz_options.pulse_interval, Some(0));
        assert_eq!(get(&fuzz_options, "pulse_interval").unwrap(), "0");

        assert!(set(&mut fuzz_options, "pulse_interval", "1m").is_ok());
        assert_eq!(fuzz_options.pulse_interval, Some(1 * NANOS_PER_MINUTE));
        assert_eq!(get(&fuzz_options, "pulse_interval").unwrap(), "\"1m\"");

        assert!(set(&mut fuzz_options, "pulse_interval", "0.25h").is_ok());
        assert_eq!(fuzz_options.pulse_interval, Some(15 * NANOS_PER_MINUTE));
        assert_eq!(get(&fuzz_options, "pulse_interval").unwrap(), "\"15m\"");

        assert!(set(&mut fuzz_options, "pulse_interval", "-1").is_err());
        assert!(set(&mut fuzz_options, "pulse_interval", "constantly").is_err());
        Ok(())
    }

    #[test]
    fn test_debug() -> Result<()> {
        let mut fuzz_options = fuzz::Options::EMPTY;
        assert!(set(&mut fuzz_options, "debug", "false").is_ok());
        assert_eq!(fuzz_options.debug, Some(false));
        assert_eq!(get(&fuzz_options, "debug").unwrap(), "false");

        assert!(set(&mut fuzz_options, "debug", "0").is_ok());
        assert_eq!(fuzz_options.debug, Some(false));
        assert_eq!(get(&fuzz_options, "debug").unwrap(), "false");

        assert!(set(&mut fuzz_options, "debug", "true").is_ok());
        assert_eq!(fuzz_options.debug, Some(true));
        assert_eq!(get(&fuzz_options, "debug").unwrap(), "true");

        assert!(set(&mut fuzz_options, "debug", "1").is_ok());
        assert_eq!(fuzz_options.debug, Some(true));
        assert_eq!(get(&fuzz_options, "debug").unwrap(), "true");

        assert!(set(&mut fuzz_options, "debug", "-1").is_err());
        assert!(set(&mut fuzz_options, "debug", "maybe").is_err());
        Ok(())
    }

    #[test]
    fn test_print_final_stats() -> Result<()> {
        let mut fuzz_options = fuzz::Options::EMPTY;
        assert!(set(&mut fuzz_options, "print_final_stats", "false").is_ok());
        assert_eq!(fuzz_options.print_final_stats, Some(false));
        assert_eq!(get(&fuzz_options, "print_final_stats").unwrap(), "false");

        assert!(set(&mut fuzz_options, "print_final_stats", "0").is_ok());
        assert_eq!(fuzz_options.print_final_stats, Some(false));
        assert_eq!(get(&fuzz_options, "print_final_stats").unwrap(), "false");

        assert!(set(&mut fuzz_options, "print_final_stats", "true").is_ok());
        assert_eq!(fuzz_options.print_final_stats, Some(true));
        assert_eq!(get(&fuzz_options, "print_final_stats").unwrap(), "true");

        assert!(set(&mut fuzz_options, "print_final_stats", "1").is_ok());
        assert_eq!(fuzz_options.print_final_stats, Some(true));
        assert_eq!(get(&fuzz_options, "print_final_stats").unwrap(), "true");

        assert!(set(&mut fuzz_options, "print_final_stats", "-1").is_err());
        assert!(set(&mut fuzz_options, "print_final_stats", "maybe").is_err());
        Ok(())
    }

    #[test]
    fn test_use_value_profile() -> Result<()> {
        let mut fuzz_options = fuzz::Options::EMPTY;
        assert!(set(&mut fuzz_options, "use_value_profile", "false").is_ok());
        assert_eq!(fuzz_options.use_value_profile, Some(false));
        assert_eq!(get(&fuzz_options, "use_value_profile").unwrap(), "false");

        assert!(set(&mut fuzz_options, "use_value_profile", "0").is_ok());
        assert_eq!(fuzz_options.use_value_profile, Some(false));
        assert_eq!(get(&fuzz_options, "use_value_profile").unwrap(), "false");

        assert!(set(&mut fuzz_options, "use_value_profile", "true").is_ok());
        assert_eq!(fuzz_options.use_value_profile, Some(true));
        assert_eq!(get(&fuzz_options, "use_value_profile").unwrap(), "true");

        assert!(set(&mut fuzz_options, "use_value_profile", "1").is_ok());
        assert_eq!(fuzz_options.use_value_profile, Some(true));
        assert_eq!(get(&fuzz_options, "use_value_profile").unwrap(), "true");

        assert!(set(&mut fuzz_options, "use_value_profile", "-1").is_err());
        assert!(set(&mut fuzz_options, "use_value_profile", "maybe").is_err());
        Ok(())
    }

    #[test]
    fn test_asan_options() -> Result<()> {
        let mut fuzz_options = fuzz::Options::EMPTY;
        assert!(set(&mut fuzz_options, "asan_options", "key1=val1").is_ok());
        assert_eq!(get(&fuzz_options, "asan_options").unwrap(), "\"key1=val1\"");

        assert!(set(&mut fuzz_options, "ubsan_options", "key2=val2").is_ok());
        assert_eq!(get(&fuzz_options, "asan_options").unwrap(), "\"\"");
        Ok(())
    }

    #[test]
    fn test_ubsan_options() -> Result<()> {
        let mut fuzz_options = fuzz::Options::EMPTY;
        assert!(set(&mut fuzz_options, "ubsan_options", "key1=val1").is_ok());
        assert_eq!(get(&fuzz_options, "ubsan_options").unwrap(), "\"key1=val1\"");

        assert!(set(&mut fuzz_options, "asan_options", "key2=val2").is_ok());
        assert_eq!(get(&fuzz_options, "ubsan_options").unwrap(), "\"\"");
        Ok(())
    }

    #[test]
    fn test_get_all() -> Result<()> {
        let mut fuzz_options = fuzz::Options::EMPTY;
        add_defaults(&mut fuzz_options);
        let all = get_all(&fuzz_options);
        let mut all = all.iter();
        fn assert_next(next: Option<&(String, String)>, name: &str, value: &str) {
            let pair = (name.to_string(), value.to_string());
            assert_eq!(next, Some(&pair));
        }
        assert_next(all.next(), "runs", "0");
        assert_next(all.next(), "max_total_time", "0");
        assert_next(all.next(), "seed", "0");
        assert_next(all.next(), "max_input_size", "\"1mb\"");
        assert_next(all.next(), "mutation_depth", "5");
        assert_next(all.next(), "dictionary_level", "0");
        assert_next(all.next(), "detect_exits", "false");
        assert_next(all.next(), "detect_leaks", "false");
        assert_next(all.next(), "run_limit", "\"20m\"");
        assert_next(all.next(), "malloc_limit", "\"2gb\"");
        assert_next(all.next(), "oom_limit", "\"2gb\"");
        assert_next(all.next(), "purge_interval", "\"1s\"");
        assert_next(all.next(), "malloc_exitcode", "2000");
        assert_next(all.next(), "death_exitcode", "2001");
        assert_next(all.next(), "leak_exitcode", "2002");
        assert_next(all.next(), "oom_exitcode", "2003");
        assert_next(all.next(), "pulse_interval", "\"20s\"");
        assert_next(all.next(), "debug", "false");
        assert_next(all.next(), "print_final_stats", "false");
        assert_next(all.next(), "use_value_profile", "false");
        assert_next(all.next(), "asan_options", "\"\"");
        assert_next(all.next(), "ubsan_options", "\"\"");
        assert!(all.next().is_none());
        Ok(())
    }
}
