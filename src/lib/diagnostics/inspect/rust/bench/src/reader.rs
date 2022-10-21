// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fuchsia_async as fasync;
use fuchsia_criterion::{criterion, FuchsiaCriterion};
use fuchsia_inspect::{
    reader::snapshot::{Snapshot, SnapshotTree},
    Inspector, NumericProperty, StringReference,
};
use futures::FutureExt;
use lazy_static::lazy_static;
use std::{
    sync::{Arc, Mutex},
    time::Duration,
};

enum InspectorState {
    Running,
    Done,
}

mod utils;

/// Start a worker thread that will continually update the given inspector at the given rate.
///
/// Returns a closure that when called cancels the thread, returning only when the thread has
/// exited.
fn start_inspector_update_thread(inspector: Inspector, changes_per_second: usize) -> impl FnOnce() {
    let state = Arc::new(Mutex::new(InspectorState::Running));
    let ret_state = state.clone();

    let child = inspector.root().create_child("test");
    let val = child.create_int("val", 0);

    let thread = std::thread::spawn(move || loop {
        let sleep_time =
            std::time::Duration::from_nanos(1_000_000_000u64 / changes_per_second as u64);
        std::thread::sleep(sleep_time);
        match *state.lock().unwrap() {
            InspectorState::Done => {
                break;
            }
            _ => {}
        }
        val.add(1);
    });

    return move || {
        {
            *ret_state.lock().unwrap() = InspectorState::Done;
        }
        thread.join().expect("join thread");
    };
}

/// Adds the given number of nodes as lazy nodes to the tree. Each lazy node will contain only one
/// int.
fn add_lazies(inspector: Inspector, num_nodes: usize) {
    let node = inspector.root().create_child("node");

    for i in 0..num_nodes {
        node.record_lazy_child(format!("child-{}", i), move || {
            let insp = Inspector::new();
            insp.root().record_int("int", 1);
            async move { Ok(insp) }.boxed()
        });
    }
}

/// This benchmark measures the performance of snapshotting an Inspect VMO of different sizes. This
/// VMO is being written concurrently at the given frequency. This is fundamental when reading
/// Inspect data in the archivist.
fn snapshot_bench(b: &mut criterion::Bencher, size: usize, frequency: usize) {
    let inspector = Inspector::new_with_size(size);
    let vmo = inspector.duplicate_vmo().expect("failed to duplicate vmo");
    let done_fn = start_inspector_update_thread(inspector, frequency);

    b.iter_with_large_drop(move || {
        criterion::black_box({
            loop {
                if let Ok(snapshot) = Snapshot::try_once_from_vmo(&vmo) {
                    break snapshot;
                }
            }
        })
    });

    done_fn();
}

/// This benchmark measures the performance of reading an Inspect VMO backed by a Tree server of
/// different sizes and containing lazy nodes. This VMO is being written concurrently at the given
/// frequency. This is fundamental when reading inspect data in the archivist.
fn snapshot_tree_bench(b: &mut criterion::Bencher, size: usize, frequency: usize) {
    let mut executor = fuchsia_async::LocalExecutor::new().unwrap();

    let inspector = Inspector::new_with_size(size);
    let (proxy, tree_server_fut) = utils::spawn_server(inspector.clone()).unwrap();
    let task = fasync::Task::spawn(tree_server_fut);

    let done_fn = start_inspector_update_thread(inspector.clone(), frequency);
    add_lazies(inspector, 4);

    b.iter_with_large_drop(|| {
        criterion::black_box({
            loop {
                if let Ok(snapshot) = executor.run_singlethreaded(SnapshotTree::try_from(&proxy)) {
                    break snapshot;
                }
            }
        })
    });

    done_fn();
    drop(proxy);
    executor.run_singlethreaded(task).unwrap();
}

/// This benchmark measures the performance of reading an Inspect VMO backed by a Tree server of
/// different sizes and containing lazy nodes.
/// This is fundamental when reading inspect data in the archivist.
fn uncontended_snapshot_tree_bench(b: &mut criterion::Bencher, size: usize) {
    let mut executor = fuchsia_async::LocalExecutor::new().unwrap();

    let inspector = Inspector::new_with_size(size);
    let (proxy, tree_server_fut) = utils::spawn_server(inspector.clone()).unwrap();
    let task = fasync::Task::local(tree_server_fut);

    b.iter_with_large_drop(|| {
        criterion::black_box({
            loop {
                if let Ok(snapshot) = executor.run_singlethreaded(SnapshotTree::try_from(&proxy)) {
                    break snapshot;
                }
            }
        })
    });

    drop(proxy);
    executor.run_singlethreaded(task).unwrap();
}

/// This benchmark measures the performance of snapshotting an Inspect VMO backed by a tree server
/// when the vmo is partially filled to the given bytes.
fn reader_snapshot_tree_vmo_bench(b: &mut criterion::Bencher, size: usize, filled_size: i64) {
    let mut executor = fuchsia_async::LocalExecutor::new().unwrap();

    let inspector = Inspector::new_with_size(size);
    let (proxy, tree_server_fut) = utils::spawn_server(inspector.clone()).unwrap();
    let task = fasync::Task::local(tree_server_fut);

    lazy_static! {
        static ref NAME: StringReference<'static> = "i".into();
    }
    let mut nodes = vec![];
    if filled_size > 0 {
        let ints_for_filling: i64 = filled_size / 16 - 1;
        for i in 0..ints_for_filling {
            nodes.push(inspector.root().create_int(&*NAME, i));
        }
    }

    b.iter_with_large_drop(|| {
        criterion::black_box({
            loop {
                if let Ok(snapshot) = executor.run_singlethreaded(SnapshotTree::try_from(&proxy)) {
                    break snapshot;
                }
            }
        })
    });

    drop(proxy);
    executor.run_singlethreaded(task).unwrap();
}

fn main() {
    let mut c = FuchsiaCriterion::default();
    let internal_c: &mut criterion::Criterion = &mut c;
    *internal_c = std::mem::take(internal_c)
        .warm_up_time(Duration::from_millis(1))
        .measurement_time(Duration::from_millis(100))
        // We must reduce the sample size from the default of 100, otherwise
        // Criterion will sometimes override the 1ms + 500ms suggested times
        // and run for much longer.
        .sample_size(10);

    // TODO(fxbug.dev/43505): Implement benchmarks where the real size doesn't match the inspector
    // size.
    // TODO(fxbug.dev/43505): Enforce threads starting before benches run.

    // SNAPSHOT BENCHMARKS
    let mut bench = criterion::Benchmark::new("Snapshot/4K/1hz", move |b| {
        snapshot_bench(b, 4096, 1);
    });
    bench = bench.with_function("Snapshot/4K/10hz", move |b| {
        snapshot_bench(b, 4096, 10);
    });
    bench = bench.with_function("Snapshot/4K/100hz", move |b| {
        snapshot_bench(b, 4096, 100);
    });
    bench = bench.with_function("Snapshot/4K/1khz", move |b| {
        snapshot_bench(b, 4096, 1000);
    });
    bench = bench.with_function("Snapshot/4K/10khz", move |b| {
        snapshot_bench(b, 4096, 10_000);
    });
    bench = bench.with_function("Snapshot/4K/100khz", move |b| {
        snapshot_bench(b, 4096, 100_000);
    });
    bench = bench.with_function("Snapshot/4K/1mhz", move |b| {
        snapshot_bench(b, 4096, 1_000_000);
    });

    bench = bench.with_function("Snapshot/256K/1hz", move |b| {
        snapshot_bench(b, 4096 * 64, 1);
    });
    bench = bench.with_function("Snapshot/256K/10hz", move |b| {
        snapshot_bench(b, 4096 * 64, 10);
    });
    bench = bench.with_function("Snapshot/256K/100hz", move |b| {
        snapshot_bench(b, 4096 * 64, 100);
    });
    bench = bench.with_function("Snapshot/256K/1khz", move |b| {
        snapshot_bench(b, 4096 * 64, 1000);
    });

    bench = bench.with_function("Snapshot/1M/1hz", move |b| {
        snapshot_bench(b, 4096 * 256, 1);
    });
    bench = bench.with_function("Snapshot/1M/10hz", move |b| {
        snapshot_bench(b, 4096 * 256, 10);
    });
    bench = bench.with_function("Snapshot/1M/100hz", move |b| {
        snapshot_bench(b, 4096 * 256, 100);
    });
    bench = bench.with_function("Snapshot/1M/1khz", move |b| {
        snapshot_bench(b, 4096 * 256, 1000);
    });

    // SNAPSHOT TREE BENCHMARKS

    bench = bench.with_function("SnapshotTree/4K/1hz", move |b| {
        snapshot_tree_bench(b, 4096, 1);
    });
    bench = bench.with_function("SnapshotTree/4K/10hz", move |b| {
        snapshot_tree_bench(b, 4096, 10);
    });
    bench = bench.with_function("SnapshotTree/4K/100hz", move |b| {
        snapshot_tree_bench(b, 4096, 100);
    });
    bench = bench.with_function("SnapshotTree/4K/1khz", move |b| {
        snapshot_tree_bench(b, 4096, 1000);
    });
    bench = bench.with_function("SnapshotTree/4K/10khz", move |b| {
        snapshot_tree_bench(b, 4096, 10_000);
    });
    bench = bench.with_function("SnapshotTree/4K/100khz", move |b| {
        snapshot_tree_bench(b, 4096, 100_000);
    });
    bench = bench.with_function("SnapshotTree/4K/1mhz", move |b| {
        snapshot_tree_bench(b, 4096, 1_000_000);
    });

    bench = bench.with_function("SnapshotTree/16K/1hz", move |b| {
        snapshot_tree_bench(b, 4096 * 4, 1);
    });
    bench = bench.with_function("SnapshotTree/16K/10hz", move |b| {
        snapshot_tree_bench(b, 4096 * 4, 10);
    });
    bench = bench.with_function("SnapshotTree/16K/100hz", move |b| {
        snapshot_tree_bench(b, 4096 * 4, 100);
    });
    bench = bench.with_function("SnapshotTree/16K/1khz", move |b| {
        snapshot_tree_bench(b, 4096 * 4, 1000);
    });

    bench = bench.with_function("SnapshotTree/256K/1hz", move |b| {
        snapshot_tree_bench(b, 4096 * 64, 1);
    });
    bench = bench.with_function("SnapshotTree/256K/10hz", move |b| {
        snapshot_tree_bench(b, 4096 * 64, 10);
    });
    bench = bench.with_function("SnapshotTree/256K/100hz", move |b| {
        snapshot_tree_bench(b, 4096 * 64, 100);
    });
    bench = bench.with_function("SnapshotTree/256K/1khz", move |b| {
        snapshot_tree_bench(b, 4096 * 64, 1000);
    });

    bench = bench.with_function("SnapshotTree/1M/1hz", move |b| {
        snapshot_tree_bench(b, 4096 * 256, 1);
    });
    bench = bench.with_function("SnapshotTree/1M/10hz", move |b| {
        snapshot_tree_bench(b, 4096 * 256, 10);
    });
    bench = bench.with_function("SnapshotTree/1M/100hz", move |b| {
        snapshot_tree_bench(b, 4096 * 256, 100);
    });
    bench = bench.with_function("SnapshotTree/1M/1khz", move |b| {
        snapshot_tree_bench(b, 4096 * 256, 1000);
    });

    // UNCONTENDED SNAPSHOT BENCHMARKS

    bench = bench.with_function("UncontendedSnapshotTree/4K", move |b| {
        uncontended_snapshot_tree_bench(b, 4096);
    });
    bench = bench.with_function("UncontendedSnapshotTree/16K", move |b| {
        uncontended_snapshot_tree_bench(b, 4096 * 4);
    });
    bench = bench.with_function("UncontendedSnapshotTree/256K", move |b| {
        uncontended_snapshot_tree_bench(b, 4096 * 64);
    });
    bench = bench.with_function("UncontendedSnapshotTree/1M", move |b| {
        uncontended_snapshot_tree_bench(b, 4096 * 256);
    });

    // PARTIALLY FILLED BENCHMARKS

    bench = bench.with_function("SnapshotTree/EmptyVMO", move |b| {
        reader_snapshot_tree_vmo_bench(b, 4096 * 256, 0);
    });
    bench = bench.with_function("SnapshotTree/QuarterFilledVMO", move |b| {
        reader_snapshot_tree_vmo_bench(b, 4096 * 256, 4096 * 64);
    });
    bench = bench.with_function("SnapshotTree/HalfFilled_VMO", move |b| {
        reader_snapshot_tree_vmo_bench(b, 4096 * 256, 4096 * 128);
    });
    bench = bench.with_function("SnapshotTree/ThreeQuarterFilledVMO", move |b| {
        reader_snapshot_tree_vmo_bench(b, 4096 * 256, 4096 * 192);
    });
    bench = bench.with_function("SnapshotTree/FullVMO", move |b| {
        reader_snapshot_tree_vmo_bench(b, 4096 * 256, 4096 * 256);
    });

    c.bench("fuchsia.rust_inspect.reader_benchmarks", bench);
}
