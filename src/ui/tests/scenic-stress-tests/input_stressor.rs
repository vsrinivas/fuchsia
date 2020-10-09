// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Error,
    fuchsia_async as fasync, fuchsia_syslog as syslog,
    input_synthesis::synthesizer::{tap_event, RegistryServerConsumer},
    log::{error, info, set_logger, set_max_level, LevelFilter},
    matches::assert_matches,
    scenic_stress_tests_lib::utils::init_scenic,
    std::time::Duration,
    structopt::StructOpt,
    test_utils_lib::{
        events::{Event, Started},
        matcher::EventMatcher,
    },
};

#[derive(Clone, StructOpt, Debug)]
#[structopt(
    name = "scenic stress test (scenic_stressor) tool",
    about = "Creates an instance of scenic and performs stressful operations on it"
)]

struct Opt {
    /// Number of operations to complete before exiting.
    #[structopt(short = "ops", long = "num_operations", default_value = "50")]
    num_operations: u64,

    /// Filter logging by level (off, error, warn, info, debug, trace)
    #[structopt(short = "l", long = "log_filter", default_value = "info")]
    log_filter: LevelFilter,

    /// Use stdout for stressor output
    #[structopt(long = "stdout")]
    stdout: bool,
}

// A simple logger that prints to stdout
struct SimpleStdoutLogger;

impl log::Log for SimpleStdoutLogger {
    fn enabled(&self, _metadata: &log::Metadata<'_>) -> bool {
        true
    }

    fn log(&self, record: &log::Record<'_>) {
        if self.enabled(record.metadata()) {
            match record.level() {
                log::Level::Info => {
                    println!("{}", record.args());
                }
                log::Level::Error => {
                    eprintln!("{}: {}", record.level(), record.args());
                }
                _ => {
                    println!("{}: {}", record.level(), record.args());
                }
            }
        }
    }

    fn flush(&self) {}
}

#[fasync::run_singlethreaded]
async fn main() -> Result<(), Error> {
    // Get arguments from command line
    let opt = Opt::from_args();

    if opt.stdout {
        // Initialize SimpleStdoutLogger
        set_logger(&SimpleStdoutLogger).expect("Failed to set SimpleLogger as global logger");
    } else {
        // Use syslog
        syslog::init().unwrap();
    }

    set_max_level(opt.log_filter);

    info!("------------------ scenic_stressor is starting -------------------");
    info!("ARGUMENTS = {:#?}", opt);
    info!("------------------------------------------------------------------");

    // Setup a panic handler that prints out details of this invocation
    let opt_clone = opt.clone();
    let default_panic_hook = std::panic::take_hook();
    std::panic::set_hook(Box::new(move |panic_info| {
        error!("");
        error!("------------------ scenic_stressor has crashed -------------------");
        error!("ARGUMENTS = {:#?}", opt_clone);
        error!("------------------------------------------------------------------");
        error!("");
        default_panic_hook(panic_info);
    }));

    // Setup the component tree and inject Scenic's dependencies
    let (test, event_source, display_state) = init_scenic().await;

    {
        let mut stream = event_source.subscribe(vec![Started::NAME]).await.unwrap();
        event_source.start_component_tree().await;

        // Wait for the root_presenter to start up
        EventMatcher::ok()
            .moniker("./root_presenter:0")
            .wait::<Started>(&mut stream)
            .await
            .unwrap();
    }

    let svc_dir_path = test.get_hub_v2_path().join("children/root_presenter/exec/out/svc");
    let svc_dir_path = svc_dir_path.to_str().unwrap().to_string();

    // Tap once every 100ms at the center of the touchscreen [num_operations] times
    let num_tap_events = opt.num_operations;
    tap_event(
        320,
        240,
        640,
        480,
        num_tap_events as usize,
        Duration::from_millis(100 * num_tap_events),
        &mut RegistryServerConsumer::new_with_path(svc_dir_path),
    )
    .unwrap();

    // The display should have updated more than 3 times.
    assert_matches!(display_state.get_num_updates(), x if x > 3);

    Ok(())
}
