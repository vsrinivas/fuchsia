// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

mod events;

use crate::events::{EventReader, EventType, InputEvent, KeyboardCode, KeyboardValue};
use std::path::Path;
use std::sync::mpsc;

/// Wait for the given key to be pressed on the given `events` iterator.
///
/// Returns `true` if the keystroke was detected before the iterator finished.
fn wait_for_key_press<I>(code: KeyboardCode, events: &mut I) -> Result<(), String>
where
    I: Iterator<Item = InputEvent>,
{
    for e in events {
        if { e.type_ } == (EventType::Key as u16)
            && e.code == (code as u16)
            && e.value == (KeyboardValue::KeyDown as i32)
        {
            return Ok(());
        }
    }
    Err("Device closed while waiting for key press.".to_string())
}

fn run_keyboard_test(input_devices: &[&Path], enable_debugging: bool) -> Result<(), String> {
    // Create a EventReader object for each input file, each with its own thread, all sending to
    // the channel `tx`.
    let (tx, rx) = mpsc::sync_channel(0);
    for path in input_devices.iter() {
        let mut reader = EventReader::new_from_path(path)
            .map_err(|e| format!("Failed to open file '{}': {}", path.display(), e))?;
        let tx = tx.clone();
        std::thread::spawn(move || {
            while let Ok(event) = reader.read() {
                tx.send(event).ok();
            }
        });
    }

    // Wait for key strokes.
    println!("Type 'abc<shift>' to pass test.");
    let mut iter = rx.iter().inspect(|&x| {
        if enable_debugging {
            println!("{:?}", x);
        }
    });
    let codes = [KeyboardCode::A, KeyboardCode::B, KeyboardCode::C, KeyboardCode::LeftShift];
    for code in &codes {
        println!("  waiting for {:?} ...", code);
        if let error @ Err(_) = wait_for_key_press(*code, &mut iter) {
            return error;
        }
    }
    println!("PASS");
    Ok(())
}

fn main() -> Result<(), String> {
    // Parse command line arguments.
    let matches = clap::App::new("VirtIO Input Tester")
        .arg(clap::Arg::with_name("debug").short("d").help("Show debugging information."))
        .subcommand(
            clap::SubCommand::with_name("keyboard")
                .about("runs a keyboard test")
                .arg(clap::Arg::with_name("files").required(true).min_values(1)),
        )
        .get_matches();
    let enable_debugging = matches.is_present("debug");

    // Run the user-specified command
    if let ("keyboard", Some(keyboard_matches)) = matches.subcommand() {
        let files =
            keyboard_matches.values_of("files").unwrap().map(Path::new).collect::<Vec<&Path>>();
        run_keyboard_test(&files, enable_debugging)
    } else {
        Err("Must provide a subcommand indicating which test to run.".to_string())
    }
}
