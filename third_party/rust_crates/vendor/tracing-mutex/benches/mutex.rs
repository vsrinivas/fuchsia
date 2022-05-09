use std::sync::Arc;
use std::sync::Mutex;

use criterion::criterion_group;
use criterion::criterion_main;
use criterion::BenchmarkId;
use criterion::Criterion;
use criterion::Throughput;
use rand::prelude::*;
use tracing_mutex::stdsync::TracingMutex;

const SAMPLE_SIZES: [usize; 5] = [10, 30, 100, 300, 1000];

/// Reproducibly generate random combinations a, b where the index(a) < index(b)
///
/// All combinations are generated
fn generate_combinations<T>(options: &[Arc<T>]) -> Vec<(Arc<T>, Arc<T>)> {
    let mut combinations = Vec::new();

    for (i, first) in options.iter().enumerate() {
        for second in options.iter().skip(i + 1) {
            combinations.push((Arc::clone(first), Arc::clone(second)));
        }
    }

    let mut rng = StdRng::seed_from_u64(42);

    combinations.shuffle(&mut rng);

    combinations
}

/// Take two arbitrary mutexes, lock the first, lock the second while holding the first.
fn benchmark_baseline(c: &mut Criterion) {
    let mut group = c.benchmark_group("baseline");

    for nodes in SAMPLE_SIZES {
        group.throughput(Throughput::Elements((nodes * (nodes - 1) / 2) as u64));
        group.bench_with_input(BenchmarkId::from_parameter(nodes), &nodes, |b, &s| {
            b.iter_batched(
                || {
                    let mutexes: Vec<_> = (0..s).map(|_| Arc::new(Mutex::new(()))).collect();
                    generate_combinations(&mutexes)
                },
                |combinations| {
                    for (first, second) in combinations {
                        let _first = first.lock();
                        let _second = second.lock();
                    }
                },
                criterion::BatchSize::SmallInput,
            )
        });
    }
}

/// Same as [`benchmark_baseline`] but now while tracking dependencies.
fn benchmark_tracing_mutex(c: &mut Criterion) {
    let mut group = c.benchmark_group("tracing_mutex");

    for nodes in SAMPLE_SIZES {
        group.throughput(Throughput::Elements((nodes * (nodes - 1) / 2) as u64));
        group.bench_with_input(BenchmarkId::from_parameter(nodes), &nodes, |b, &s| {
            b.iter_batched(
                || {
                    let mutexes: Vec<_> = (0..s).map(|_| Arc::new(TracingMutex::new(()))).collect();
                    generate_combinations(&mutexes)
                },
                |combinations| {
                    for (first, second) in combinations {
                        let _first = first.lock();
                        let _second = second.lock();
                    }
                },
                criterion::BatchSize::SmallInput,
            )
        });
    }
}

criterion_group!(benches, benchmark_baseline, benchmark_tracing_mutex);
criterion_main!(benches);
