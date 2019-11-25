// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

mod args;
mod common_operations;
mod file_target;
mod generator;
mod io_packet;
mod issuer;
mod log;
mod operations;
mod sequential_io_generator;
mod target;
mod verifier;

use {
    crate::generator::{run_load, GeneratorArgs},
    crate::log::{log_init, Stats},
    ::log::{debug, log_enabled, Level::Debug},
    failure::Error,
    std::{
        fs::{metadata, File, OpenOptions},
        io::prelude::*,
        process,
        sync::{Arc, Mutex},
        thread::spawn,
        time::Instant,
    },
};

// Magic number that gets written in block header
static MAGIC_NUMBER: u64 = 0x4f6475346573742e;

fn create_target(target_name: &String, target_length: u64) {
    let metadata = metadata(&target_name);

    match metadata {
        Ok(stats) => {
            assert!(!stats.permissions().readonly());
            assert!(stats.len() >= target_length);
            return;
        }
        _ => {}
    }
    // TODO(auradkar): File should not be created here. It is generator/target's
    // knowledge/job/responsibility.
    let f = File::create(&target_name).unwrap();

    // Note: Though the target length can be any arbitrary value, the IOs issued
    // on the target's range vary depending on various parameters such as
    // max_io_size, max_io_count, thread_count, align, etc. It may happen that
    // some portion of the target may never gets IOs.
    f.set_len(target_length).unwrap();
}

fn output_config(generator_args_vec: &Vec<GeneratorArgs>, output_config_file: &String) {
    let serialized = serde_json::to_string_pretty(&generator_args_vec).unwrap();
    let mut file = OpenOptions::new()
        .create(true)
        .write(true)
        .truncate(true)
        .open(&output_config_file)
        .unwrap();
    file.write_all(serialized.as_bytes()).unwrap();

    debug!("{}", serialized);
    file.sync_data().unwrap();
}

fn main() -> Result<(), Error> {
    log_init()?;

    let args = args::parse()?;

    let start_instant: Instant = Instant::now();

    let mut thread_handles = vec![];
    let mut generator_args_vec = vec![];

    create_target(&args.target, args.target_length);

    let metadata = metadata(&args.target)?;

    let mut offset_start = 0 as u64;
    let range_size = (metadata.len() / args.thread_count as u64) as u64;

    // To keep contention among threads low, each generator owns/updates their
    // stats. The "main" thread holds a
    // reference to these stats so that, when ready, it can print the
    // progress/stats from time to time.
    let mut stats_array = Vec::with_capacity(args.thread_count);

    for i in 0..args.thread_count {
        let generator_args = GeneratorArgs::new(
            MAGIC_NUMBER,
            process::id() as u64,
            i as u64, // generator id
            args.block_size,
            args.max_io_size,
            args.align,
            i as u64, // seed
            args.target.clone(),
            offset_start..(offset_start + range_size),
            args.target_type,
            args.operations.clone(),
            args.queue_depth,
            args.max_io_count,
            args.sequential,
        );
        generator_args_vec.push(generator_args.clone());

        let stats = {
            let mut stats = Stats::new();
            stats.start_clock();
            Arc::new(Mutex::new(stats))
        };
        stats_array.push(stats.clone());
        thread_handles.push(spawn(move || run_load(generator_args, start_instant, stats)));
        offset_start += range_size;
    }

    output_config(&generator_args_vec, &args.output_config_file.to_string());
    for handle in thread_handles {
        handle.join().unwrap()?;
    }

    let mut aggregate_stats = Stats::new();
    let mut i = 0;

    // How the summary stats. For long running IO load we should print the stats
    // from time to time to show how IO are going. A TODO(auradkar).
    for stat in stats_array {
        let stat = stat.lock().unwrap();
        aggregate_stats.aggregate_summary(&stat);
        if log_enabled!(Debug) {
            debug!("===== For generator-{} =====", i);
            stat.display_summary();
            i += 1;
        }
    }
    println!("===== Aggregate Stats =====");
    aggregate_stats.display_summary();

    Ok(())
}
