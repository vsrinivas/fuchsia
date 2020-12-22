// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

mod lib;
mod protocol;

use {
    anyhow::{format_err, Error},
    clap::{App, Arg, ArgMatches, SubCommand},
    serde_json as json,
    std::{
        fs::File,
        io::{Read, Write},
        sync::mpsc,
        time::Duration,
    },
};

/// Describes allowable values for the --duration arg of `record`.
const DURATION_REGEX: &'static str = r"^(\d+)(h|m|s|ms)$";

const ZEDMON_NOMINAL_DATA_RATE_HZ: u32 = 1500;
const ZEDMON_NOMINAL_DATA_INTERVAL_USEC: f32 = 1e6 / ZEDMON_NOMINAL_DATA_RATE_HZ as f32;

/// Validates the --duration arg of `record`.
fn validate_duration(value: String) -> Result<(), String> {
    let re = regex::Regex::new(DURATION_REGEX).unwrap();
    if re.is_match(&value) {
        Ok(())
    } else {
        Err(format!("Duration must match the regex {}", DURATION_REGEX))
    }
}

/// Parses the --duration arg of `record`.
fn parse_duration(value: &str) -> Duration {
    let re = regex::Regex::new(DURATION_REGEX).unwrap();
    let captures = re.captures(&value).unwrap();
    let number: u64 = captures[1].parse().unwrap();
    let unit = &captures[2];

    match unit {
        "ms" => Duration::from_millis(number),
        "s" => Duration::from_secs(number),
        "m" => Duration::from_secs(number * 60),
        "h" => Duration::from_secs(number * 3600),
        _ => panic!("Invalid duration string: {}", value),
    }
}

fn validate_downsampling_interval(value: String) -> Result<(), String> {
    validate_duration(value.clone())?;
    let interval = parse_duration(&value);
    if interval.as_secs_f32() * 1e6 > ZEDMON_NOMINAL_DATA_INTERVAL_USEC {
        Ok(())
    } else {
        Err(format!("Value must be greater than {}us", ZEDMON_NOMINAL_DATA_INTERVAL_USEC))
    }
}

fn main() -> Result<(), Error> {
    let matches = App::new("zedmon")
        .about("Utility for interacting with Zedmon power measurement device")
        .subcommand(
            SubCommand::with_name("describe")
                .about("Describes properties of the device and/or client.")
                .arg(
                    Arg::with_name("name")
                    .help(
                        "Optional name of a parameter to look up. If provided, only the value will \
                        be printed. Otherwise, all parameter names and values will be printed in \
                        JSON format.")
                    .required(false)
                    .index(1)
                    .possible_values(&lib::DESCRIBABLE_PROPERTIES),
                ),
        )
        .subcommand(
            SubCommand::with_name("list").about("Lists serial number of connected Zedmon devices"),
        )
        .subcommand(
            SubCommand::with_name("record").about("Record power data").arg(
                Arg::with_name("out")
                    .help("Name of output file. Use '-' for stdout.")
                    .short("o")
                    .long("out")
                    .takes_value(true)
            ).arg(
                Arg::with_name("average")
                    .help(
                        &format!(
                            "Specifies that the client will output exactly one record, which \
                            averages data over the specified duration. This is equivalent to \
                            setting --duration and --interval to the same value. If specified, \
                            must match the regular expression '{}'.",
                            DURATION_REGEX)
                        )
                    .short("a")
                    .long("average")
                    .takes_value(true)
                    .value_name("duration")
                    .validator(&validate_duration)
                    .conflicts_with_all(&["duration", "interval"]),
            ).arg(
                Arg::with_name("duration")
                    .help(
                        &format!(
                            "Duration of time on the Zedmon device to be spanned by data \
                            recording. If omitted, recording will continue until ENTER is pressed. \
                            If specified, must match the regular expression '{}'.",
                            DURATION_REGEX)
                        )
                    .short("d")
                    .long("duration")
                    .takes_value(true)
                    .validator(&validate_duration)
                    .conflicts_with("average"),
            ).arg(
                Arg::with_name("interval")
                    .help(
                        &format!(
                            "Interval at which to report data. Raw measurements from Zedmon will \
                            be averaged at this interval. \
                            \n  If --interval is omitted, each sample will be reported. If \
                            specified, it must match the regular expression '{}'. It must also be \
                            greater than {:.1}us, Zemdon's nominal reporting interval (corresponding \
                            to {} Hz). \
                            \n  If a gap in raw data contains multiple downsampling output times, \
                            then no samples will be emitted during the gap, and the downsampling \
                            process will reinitialize with the end of the gap as its starting \
                            point.",
                            DURATION_REGEX,
                            ZEDMON_NOMINAL_DATA_INTERVAL_USEC,
                            ZEDMON_NOMINAL_DATA_RATE_HZ)
                        )
                    .short("i")
                    .long("interval")
                    .takes_value(true)
                    .value_name("duration")
                    .validator(&validate_downsampling_interval)
                    .conflicts_with("average"),
            )
        )
        .subcommand(
            SubCommand::with_name("relay").about("Enables/disables relay").arg(
                Arg::with_name("state")
                    .help("State of the relay: 'on' or 'off'")
                    .required(true)
                    .index(1)
                    .possible_values(&["on", "off"]),
            ),
        )
        .get_matches();

    match matches.subcommand() {
        ("describe", Some(arg_matches)) => Ok(run_describe(arg_matches)),
        ("list", _) => run_list(),
        ("record", Some(arg_matches)) => run_record(arg_matches),
        ("relay", Some(arg_matches)) => run_relay(arg_matches),
        _ => panic!("Invalid subcommand"),
    }
}

fn run_describe(arg_matches: &ArgMatches<'_>) {
    let zedmon = lib::zedmon();
    match arg_matches.value_of("name") {
        Some(name) => println!("{}", zedmon.describe(name).unwrap().to_string()),
        None => {
            let params = lib::DESCRIBABLE_PROPERTIES
                .iter()
                .map(|name| (name.to_string(), zedmon.describe(name).unwrap()))
                .collect();
            println!("{}", json::to_string_pretty(&json::Value::Object(params)).unwrap());
        }
    }
}

/// Runs the "list" subcommand.
fn run_list() -> Result<(), Error> {
    let serials = lib::list();
    if serials.is_empty() {
        Err(format_err!("No Zedmon devices found"))
    } else {
        for serial in serials {
            println!("{}", serial);
        }
        Ok(())
    }
}

/// Raises a stop signal for Zedmon recording upon the first input to stdin (which, given stdin
/// buffering, means on the first press of ENTER.)
struct StdinStopper {
    receiver: mpsc::Receiver<()>,
    stopped: bool,
}

impl StdinStopper {
    fn new() -> StdinStopper {
        let (sender, receiver) = mpsc::sync_channel(1);
        std::thread::spawn(move || {
            let mut stdin = std::io::stdin();
            let mut buffer = [0u8; 1];
            loop {
                match stdin.read(&mut buffer) {
                    Ok(_) => {
                        sender.send(()).unwrap();
                        return;
                    }
                    Err(e) => eprintln!("Error reading from stdin: {:?}", e),
                }
            }
        });

        StdinStopper { receiver, stopped: false }
    }
}

impl lib::StopSignal for StdinStopper {
    fn should_stop(&mut self, _: u64) -> Result<bool, Error> {
        match self.receiver.try_recv() {
            Ok(()) => self.stopped = true,
            Err(mpsc::TryRecvError::Empty) => {}
            Err(mpsc::TryRecvError::Disconnected) => {
                return Err(format_err!("stdin sender was disconnected before signalling."));
            }
        }
        Ok(self.stopped)
    }
}

/// Runs the "record" subcommand".
fn run_record(arg_matches: &ArgMatches<'_>) -> Result<(), Error> {
    // Parse --out.
    let (output, dest_name): (Box<dyn Write + Send>, &str) = match arg_matches.value_of("out") {
        None => (Box::new(File::create("zedmon.csv")?), "zedmon.csv"),
        Some("-") => (Box::new(std::io::stdout()), "stdout"),
        Some(filename) => (Box::new(File::create(filename)?), filename),
    };
    let dest_name = dest_name.to_string();

    // Parse either --average or --duration and --interval.
    let (duration, reporting_interval) = match arg_matches.value_of("average") {
        Some(value) => {
            let duration = parse_duration(value);
            (Some(duration), Some(duration))
        }
        None => (
            arg_matches.value_of("duration").map(parse_duration),
            arg_matches.value_of("interval").map(parse_duration),
        ),
    };

    let zedmon = lib::zedmon();

    // TODO(fxbug.dev/61471): Consider incorporating the time offset directly into report
    // timestamps.
    let (offset, uncertainty) = zedmon.get_time_offset_nanos()?;
    println!("Time offset: {}ns ± {}ns\n", offset, uncertainty);

    println!("Recording to {}.", dest_name);
    match duration {
        Some(duration) => {
            zedmon.read_reports(output, lib::DurationStopper::new(duration), reporting_interval)
        }
        None => {
            println!("Press ENTER to stop.");
            zedmon.read_reports(output, StdinStopper::new(), reporting_interval)
        }
    }
}

/// Runs the "relay" subcommand.
fn run_relay(arg_matches: &ArgMatches<'_>) -> Result<(), Error> {
    let zedmon = lib::zedmon();
    zedmon.set_relay(arg_matches.value_of("state").unwrap() == "on")?;
    Ok(())
}
