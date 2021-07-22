// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fuchsia_criterion::{
    criterion::{self, Criterion},
    FuchsiaCriterion,
};

use archivist_lib::logs::buffer::{ArcList, LazyItem};
use fidl_fuchsia_diagnostics::StreamMode;
use fuchsia_async as fasync;
use fuchsia_async::futures::StreamExt;
use std::convert::TryInto;
use std::mem;
use std::sync::atomic::AtomicBool;
use std::sync::Arc;
use std::time::Duration;

fn bench_fill(b: &mut criterion::Bencher, size: usize) {
    b.iter(|| {
        let list = ArcList::<usize>::default();
        for _ in 0..size {
            list.push_back(100);
        }
    });
}

fn bench_fill_drain(b: &mut criterion::Bencher, size: usize) {
    b.iter(|| {
        let list = ArcList::<usize>::default();
        for _ in 0..size {
            list.push_back(100);
        }
        while !list.is_empty() {
            criterion::black_box(list.pop_front());
        }
    });
}

#[derive(Copy, Clone)]
struct IterateArgs {
    size: usize,
    write_threads: usize,
    writes_per_second: usize,
}

fn bench_iterate_concurrent(b: &mut criterion::Bencher, args: IterateArgs) {
    let list = ArcList::<usize>::default();
    let done = Arc::new(AtomicBool::new(false));
    for _ in 0..args.size {
        // fill the list
        list.push_back(100);
    }

    // create writer threads that constantly pop and push values
    // the overall size of the list should remain approximately the same
    let mut threads = vec![];
    for _ in 0..args.write_threads {
        let done = done.clone();
        let list = list.clone();
        threads.push(std::thread::spawn(move || {
            while !done.load(std::sync::atomic::Ordering::Relaxed) {
                list.pop_front();
                list.push_back(200);
                std::thread::sleep(Duration::from_micros(
                    (1_000_000 / args.writes_per_second).try_into().unwrap(),
                ));
            }
        }));
    }

    // measure how long it takes to read |size| entries from the list
    let mut executor = fasync::LocalExecutor::new().unwrap();
    b.iter(|| {
        let list = list.clone();
        executor.run_singlethreaded(async move {
            let mut items_read = 0;
            let mut cursor = list.cursor(StreamMode::SnapshotThenSubscribe);
            while items_read < args.size {
                match cursor.next().await.expect("must have some value") {
                    LazyItem::Next(_) => {
                        items_read += 1;
                    }
                    _ => {}
                }
            }
        });
    });

    done.store(true, std::sync::atomic::Ordering::Relaxed);

    for thread in threads {
        thread.join().unwrap();
    }
}

fn main() {
    let mut c = FuchsiaCriterion::default();
    let internal_c: &mut Criterion = &mut c;
    *internal_c = mem::take(internal_c)
        .warm_up_time(Duration::from_secs(2))
        .measurement_time(Duration::from_secs(2))
        .sample_size(20);

    let mut bench = criterion::Benchmark::new("Logging/ArcList/CheckEmpty", move |b| {
        let list = ArcList::<usize>::default();
        b.iter(|| criterion::black_box(list.is_empty()));
    });

    // The following benchmarks measure the performance of ArcList, the fundamental data
    // structured used to store components' logs.

    // Benchmark the time needed to fill an ArcList with 16K entries.
    // This measures the performance of the underlying data structure for storing logs.
    bench = bench.with_function("Logging/ArcList/Fill/16K", move |b| {
        bench_fill(b, 16 * 1024);
    });

    // Benchmark the time needed to fill and then drain an ArcList for 16K entries.
    // This measures the performance of rotating log buffers.
    bench = bench.with_function("Logging/ArcList/FillDrain/16K", move |b| {
        bench_fill_drain(b, 16 * 1024);
    });

    // Benchmark the time needed to read 16K entries from an ArcList starting with 16K entries
    // while concurrent threads are writing to the list.
    // This measures the scenario of reading a fixed amount of logs while numerous components
    // continue writing and rotating logs to the buffer.

    // This benchmark has no concurrent writers, so it measures the baseline to read all 16K logs
    // out of the buffer.
    bench =
        bench.with_function("Logging/ArcList/Iterate/size=16K/writers=0/per_second=1", move |b| {
            bench_iterate_concurrent(
                b,
                IterateArgs { size: 16 * 1024, write_threads: 0, writes_per_second: 1 },
            );
        });

    // This benchmark has one concurrent writer pushing 50 logs per second.
    // It measures the overhead of concurrent write locking on the reader.
    bench =
        bench.with_function("Logging/ArcList/Iterate/size=16K/writers=1/per_second=50", move |b| {
            bench_iterate_concurrent(
                b,
                IterateArgs { size: 16 * 1024, write_threads: 1, writes_per_second: 50 },
            );
        });

    // Same as above, but with 500 logs per second being written.
    bench = bench.with_function(
        "Logging/ArcList/Iterate/size=16K/writers=1/per_second=500",
        move |b| {
            bench_iterate_concurrent(
                b,
                IterateArgs { size: 16 * 1024, write_threads: 1, writes_per_second: 500 },
            );
        },
    );

    // Same as above, but with 3 threads each writing 500 logs per second.
    bench = bench.with_function(
        "Logging/ArcList/Iterate/size=16K/writers=3/per_second=500",
        move |b| {
            bench_iterate_concurrent(
                b,
                IterateArgs { size: 16 * 1024, write_threads: 3, writes_per_second: 500 },
            );
        },
    );

    c.bench("fuchsia.archivist", bench);
}
