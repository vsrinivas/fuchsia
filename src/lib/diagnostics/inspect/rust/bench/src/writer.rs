// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fuchsia_async as fasync;
use fuchsia_criterion::{criterion, FuchsiaCriterion};
use fuchsia_inspect::{
    reader::ReadableTree, ArithmeticArrayProperty, ArrayProperty, ExponentialHistogramParams, Heap,
    HistogramProperty, Inspector, LinearHistogramParams, NumericProperty, Property,
};
use mapped_vmo::Mapping;
use num::{pow, traits::FromPrimitive, One};
use rand::Rng;
use std::ops::{Add, Mul};
use std::{sync::Arc, time::Duration};

const NAME: &str = "name";

mod utils;

/// Benchmarks for operations that can be done on an Inspect Node.
fn node_benchmarks(mut bench: criterion::Benchmark) -> criterion::Benchmark {
    bench = bench.with_function("Node/create_child", move |b| {
        let inspector = Inspector::new();
        let root = inspector.root();
        b.iter_with_large_drop(|| root.create_child(NAME));
    });
    bench = bench.with_function("Node/drop", move |b| {
        let inspector = Inspector::new();
        let root = inspector.root();
        b.iter_with_large_setup(|| root.create_child(NAME), |child| drop(child));
    });
    bench
}

/// Generates benchmarks for operations that can be done on Inspect numeric properties.
macro_rules! bench_numeric_property_fn {
    ($name:ident, $type:ty, $Property:expr) => {
        paste::paste! {
            fn [<$name _property_benchmarks>](
                mut bench: criterion::Benchmark
            ) -> criterion::Benchmark {
                bench = bench.with_function(
                    concat!("Node/create_", stringify!($name), "_property"),
                    move |b| {
                        let inspector = Inspector::new();
                        let root = inspector.root();
                        b.iter_with_large_drop(|| root.[<create_ $name>](NAME, 0 as $type));
                    }
                );
                bench = bench.with_function(concat!($Property, "/set"), move |b| {
                    let inspector = Inspector::new();
                    let root = inspector.root();
                    let property = root.[<create_ $name>](NAME, 0 as $type);
                    b.iter(|| property.set(1 as $type));
                });
                bench = bench.with_function(concat!($Property, "/add"), move |b| {
                    let inspector = Inspector::new();
                    let root = inspector.root();
                    let property = root.[<create_ $name>](NAME, 0 as $type);
                    b.iter(|| property.add(1 as $type));
                });
                bench = bench.with_function(concat!($Property, "/subtract"), move |b| {
                    let inspector = Inspector::new();
                    let root = inspector.root();
                    let property = root.[<create_ $name>](NAME, $type::MAX);
                    b.iter(|| property.subtract(1 as $type));
                });
                bench = bench.with_function(concat!($Property, "/drop"), move |b| {
                    let inspector = Inspector::new();
                    let root = inspector.root();
                    b.iter_with_large_setup(
                        || root.[<create_ $name>](NAME, 0 as $type),
                        |p| drop(p));
                });
                bench = bench.with_function(
                    concat!("Node/record_", stringify!($name), "_property"),
                    move |b| {
                        let inspector = Inspector::new();
                        b.iter_with_large_setup(
                            || inspector.root().create_child(NAME),
                            |child| child.[<record_ $name>](NAME, 0 as $type));
                    }
                );
                bench
            }
        }
    };
}

/// Returns a string value to use in benchmarks.
fn get_string_value(size: usize) -> String {
    std::iter::repeat("a").take(size).collect::<String>()
}

/// Returns a bytes value to use in benchmarks.
fn get_bytes_value(size: usize) -> Vec<u8> {
    vec![1; size]
}

/// Generates benchmarks for operations that can be done on Inspect string and bytes properties.
macro_rules! bench_property_fn {
    ($name:ident, $Property:expr) => {
        paste::paste! {
            fn [<$name _property_benchmarks>](
                mut bench: criterion::Benchmark,
                size: usize,
            ) -> criterion::Benchmark {
                let initial_value = [<get_ $name _value>](0);
                let value = [<get_ $name _value>](size);

                let initial_val = initial_value.clone();
                bench = bench.with_function(
                    format!("Node/create_{}/{}", stringify!($name), size),
                    move |b| {
                        let inspector = Inspector::new();
                        let root = inspector.root();
                        b.iter_with_large_drop(|| root.[<create_ $name>](NAME, &initial_val));
                    }
                );

                let value_for_bench = value.clone();
                let initial_val = initial_value.clone();
                bench = bench.with_function(
                    format!("{}/set/{}", $Property, size),
                    move |b| {
                        let inspector = Inspector::new();
                        let root = inspector.root();
                        b.iter_with_large_setup(
                            || {
                                root.[<create_ $name>](NAME, &initial_val)
                            },
                            |property| property.set(&value_for_bench),
                        );
                    }
                );

                let value_for_bench = value.clone();
                let initial_val = initial_value.clone();
                bench = bench.with_function(
                    format!("{}/set_again/{}", $Property, size),
                    move |b| {
                        let inspector = Inspector::new();
                        let root = inspector.root();
                        b.iter_with_large_setup(
                            || {
                                let property = root.[<create_ $name>](NAME, &initial_val);
                                property.set(&value_for_bench);
                                property
                            },
                            |property| property.set(&value_for_bench),
                        );
                    }
                );

                let initial_val = initial_value.clone();
                bench = bench.with_function(format!("{}/drop/{}", $Property, size), move |b| {
                    let inspector = Inspector::new();
                    let root = inspector.root();
                    b.iter_with_large_setup(
                        || root.[<create_ $name>](NAME, &initial_val),
                        |p| drop(p));
                });
                let initial_val = initial_value.clone();
                bench = bench.with_function(
                    format!("Node/record_{}/{}", stringify!($name), size),
                    move |b| {
                        let inspector = Inspector::new();
                        b.iter_with_large_setup(
                            || inspector.root().create_child(NAME),
                            |child| child.[<record_ $name>](NAME, &initial_val));
                    }
                );
                bench
            }
        }
    };
}

/// Generates benchmarks for operations that can be done on Inspect numeric array properties.
macro_rules! bench_numeric_array_property_fn {
    ($name:ident, $type:ty, $Array:expr) => {
        paste::paste! {
            fn [<$name _array_property_benchmarks>](
                mut bench: criterion::Benchmark,
                array_size: usize
            ) -> criterion::Benchmark {
                bench = bench.with_function(
                    format!("Node/create_{}_array/{}", stringify!($name), array_size),
                    move |b| {
                        let inspector = Inspector::new();
                        let root = inspector.root();
                        b.iter_with_large_drop(|| root.[<create_ $name _array>](NAME, array_size));
                    });
                bench = bench.with_function(
                    format!("{}/set/{}", $Array, array_size),
                    move |b| {
                        let mut rng = rand::thread_rng();
                        let inspector = Inspector::new();
                        let root = inspector.root();
                        let array = root.[<create_ $name _array>](NAME, array_size);
                        b.iter_with_large_setup(|| rng.gen_range(0..array_size), |index| {
                            array.set(index, 1 as $type);
                        });
                    }
                );
                bench = bench.with_function(
                    format!("{}/add/{}", $Array, array_size),
                    move |b| {
                        let mut rng = rand::thread_rng();
                        let inspector = Inspector::new();
                        let root = inspector.root();
                        let array = root.[<create_ $name _array>](NAME, array_size);
                        b.iter_with_large_setup(|| rng.gen_range(0..array_size), |index| {
                            array.add(index, 1 as $type);
                        });
                    }
                );
                bench = bench.with_function(
                    format!("{}/subtract/{}", $Array, array_size),
                    move |b| {
                        let mut rng = rand::thread_rng();
                        let inspector = Inspector::new();
                        let root = inspector.root();
                        let array = root.[<create_ $name _array>](NAME, array_size);
                        for i in 0..array_size {
                            array.set(i, $type::MAX);
                        }
                        b.iter_with_large_setup(|| rng.gen_range(0..array_size), |index| {
                            array.subtract(index, 1 as $type);
                        });
                    }
                );
                bench = bench.with_function(
                    format!("{}/drop/{}", $Array, array_size),
                    move |b| {
                        let inspector = Inspector::new();
                        let root = inspector.root();
                        b.iter_with_large_setup(
                            || root.[<create_ $name _array>](NAME, array_size),
                            |p| drop(p));
                    }
                );
                bench
           }
        }
    };
}

/// Generates benchmarks for operations that can be done on Inspect string array properties.
fn string_array_property_benchmarks(
    mut bench: criterion::Benchmark,
    array_size: usize,
) -> criterion::Benchmark {
    bench = bench.with_function(format!("Node/create_string_array/{}", array_size), move |b| {
        let inspector = Inspector::new();
        let root = inspector.root();
        b.iter_with_large_drop(|| root.create_string_array(NAME, array_size));
    });
    bench = bench.with_function(format!("StringArrayProperty/set/{}", array_size), move |b| {
        let mut rng = rand::thread_rng();
        let inspector = Inspector::new();
        let root = inspector.root();
        let array = root.create_string_array(NAME, array_size);
        b.iter_with_large_setup(
            || rng.gen_range(0..array_size),
            |index| {
                array.set(index, "one");
            },
        );
    });
    bench = bench.with_function(format!("StringArrayProperty/clear/{}", array_size), move |b| {
        let inspector = Inspector::new();
        let root = inspector.root();
        let array = root.create_string_array(NAME, array_size);
        b.iter(|| {
            array.clear();
        });
    });
    bench = bench.with_function(format!("StringArrayProperty/drop/{}", array_size), move |b| {
        let inspector = Inspector::new();
        let root = inspector.root();
        b.iter_with_large_setup(|| root.create_string_array(NAME, array_size), |p| drop(p));
    });
    bench
}

/// Returns linear histogram parameters and a value that for use in linear histogram benchmarks.
fn get_linear_bench_data<T>(size: usize) -> (LinearHistogramParams<T>, T)
where
    T: FromPrimitive + Add<Output = T> + Mul<Output = T> + Copy,
{
    let params = LinearHistogramParams {
        floor: T::from_usize(10).unwrap(),
        step_size: T::from_usize(5).unwrap(),
        buckets: size,
    };
    let x = T::from_usize(size / 2).unwrap();
    let initial_value = params.floor + x * params.step_size;
    (params, initial_value)
}

/// Returns exponential histogram parameters and a value that for use in exponential histogram
/// benchmarks.
fn get_exponential_bench_data<T>(size: usize) -> (ExponentialHistogramParams<T>, T)
where
    T: FromPrimitive + Add<Output = T> + Mul<Output = T> + One<Output = T> + Clone + Copy,
{
    let params = ExponentialHistogramParams {
        floor: T::from_usize(10).unwrap(),
        initial_step: T::from_usize(5).unwrap(),
        step_multiplier: T::from_usize(2).unwrap(),
        buckets: size,
    };
    let initial_value = params.floor + params.initial_step * pow(params.step_multiplier, size / 8);
    (params, initial_value)
}

/// Generates benchmarks for operations that can be done on Inspect histogram properties.
macro_rules! bench_histogram_property_fn {
    ($name:ident, $type:ty, $Histogram:expr, $histogram_type:ident) => {
        paste::paste! {
            fn [<$name _ $histogram_type _histogram_property_benchmarks>](
                mut bench: criterion::Benchmark,
                size: usize,
            ) -> criterion::Benchmark {
                let (parameters, value) = [<get_ $histogram_type _bench_data>](size);

                let params = parameters.clone();
                bench = bench.with_function(
                    format!(
                        "Node/create_{}_{}_histogram/{}",
                        stringify!($name), stringify!($histogram_type), size
                    ),
                    move |b| {
                        let inspector = Inspector::new();
                        let root = inspector.root();
                        b.iter_with_large_drop(|| {
                            root.[<create_ $name _ $histogram_type _histogram>](NAME, params.clone())
                        });
                    }
                );

                let params = parameters.clone();
                bench = bench.with_function(
                    format!("{}/insert/{}", $Histogram, size),
                    move |b| {
                        let inspector = Inspector::new();
                        let root = inspector.root();
                        b.iter_with_large_setup(
                            || {
                                root.[<create_ $name _ $histogram_type _histogram>](
                                    NAME, params.clone())
                            },
                            |property| property.insert(value),
                        );
                    }
                );

                let params = parameters.clone();
                bench = bench.with_function(
                    format!("{}/insert_overflow/{}", $Histogram, size),
                    move |b| {
                        let inspector = Inspector::new();
                        let root = inspector.root();
                        b.iter_with_large_setup(
                            || {
                                root.[<create_ $name _ $histogram_type _histogram>](
                                    NAME, params.clone())
                            },
                            |property| property.insert(10_000_000 as $type),
                        );
                    }
                );

                let params = parameters.clone();
                bench = bench.with_function(
                    format!("{}/insert_underflow/{}", $Histogram, size),
                    move |b| {
                        let inspector = Inspector::new();
                        let root = inspector.root();
                        b.iter_with_large_setup(
                            || {
                                root.[<create_ $name _ $histogram_type _histogram>](
                                    NAME, params.clone())
                            },
                            |property| property.insert(0 as $type),
                        );
                    }
                );

                let params = parameters.clone();
                bench = bench.with_function(format!("{}/drop/{}", $Histogram, size), move |b| {
                    let inspector = Inspector::new();
                    let root = inspector.root();
                    b.iter_with_large_setup(
                        || root.[<create_ $name _ $histogram_type _histogram>](
                            NAME, params.clone()),
                        |p| drop(p));
                });

                bench
            }
        }
    };
}

/// Benchmark the Inspect VMO heap extend mechanism.
fn bench_heap_extend(mut bench: criterion::Benchmark) -> criterion::Benchmark {
    bench = bench.with_function("Heap/create_1mb_vmo", |b| {
        b.iter_with_large_drop(|| {
            let (mapping, _) = Mapping::allocate(1 << 21).unwrap();
            Heap::new(Arc::new(mapping)).unwrap()
        });
    });
    bench = bench.with_function("Heap/allocate_512k", |b| {
        b.iter_with_large_setup(
            || {
                let (mapping, _) = Mapping::allocate(1 << 21).unwrap();
                Heap::new(Arc::new(mapping)).unwrap()
            },
            |mut heap| {
                for _ in 0..512 {
                    heap.allocate_block(2048).unwrap();
                }
            },
        );
    });
    bench = bench.with_function("Heap/extend", |b| {
        b.iter_with_large_setup(
            || {
                let (mapping, _) = Mapping::allocate(1 << 21).unwrap();
                let mut heap = Heap::new(Arc::new(mapping)).unwrap();
                for _ in 0..512 {
                    heap.allocate_block(2048).unwrap();
                }
                heap
            },
            |mut heap| heap.allocate_block(2048).unwrap(),
        );
    });
    bench = bench.with_function("Heap/free", |b| {
        b.iter_with_large_setup(
            || {
                let (mapping, _) = Mapping::allocate(1 << 21).unwrap();
                let mut heap = Heap::new(Arc::new(mapping)).unwrap();
                let mut blocks = vec![];
                for _ in 0..512 {
                    blocks.push(heap.allocate_block(2048).unwrap());
                }
                (blocks, heap)
            },
            |(blocks, mut heap)| {
                for block in blocks {
                    heap.free_block(block).unwrap();
                }
            },
        );
    });
    bench = bench.with_function("Heap/drop", |b| {
        b.iter_with_large_setup(
            || {
                let (mapping, _) = Mapping::allocate(1 << 21).unwrap();
                Heap::new(Arc::new(mapping)).unwrap()
            },
            |heap| drop(heap),
        );
    });

    bench
}

/// Benchmark the write-speed of a local inspector after it has been copy-on-write
/// served over FIDL
fn bench_write_after_tree_cow_read(mut bench: criterion::Benchmark) -> criterion::Benchmark {
    let mut executor = fasync::LocalExecutor::new().unwrap();
    let inspector = Inspector::new();

    let mut properties = vec![];

    for i in 0..1015 {
        properties.push(inspector.root().create_int("i", i));
    }

    let (proxy, tree_server_fut) = utils::spawn_server(inspector.clone()).unwrap();
    let task = fasync::Task::spawn(tree_server_fut);
    // Force TLB shootdown for following writes on the local inspector
    let _ = proxy.vmo();

    bench = bench.with_function("Node/IntProperty::CoW::Add", move |b| {
        b.iter(|| {
            for p in &properties {
                p.add(1);
            }
        });
    });

    drop(proxy);
    executor.run_singlethreaded(task).unwrap();
    bench
}

bench_numeric_property_fn!(int, i64, "IntProperty");
bench_numeric_property_fn!(uint, u64, "UintProperty");
bench_numeric_property_fn!(double, f64, "DoubleProperty");
bench_property_fn!(string, "StringProperty");
bench_property_fn!(bytes, "BytesProperty");
bench_numeric_array_property_fn!(int, i64, "IntArrayProperty");
bench_numeric_array_property_fn!(uint, u64, "UintArrayProperty");
bench_numeric_array_property_fn!(double, f64, "DoubleArrayProperty");
bench_histogram_property_fn!(int, i64, "IntLinearHistogramProperty", linear);
bench_histogram_property_fn!(uint, u64, "UintLinearHistogramProperty", linear);
bench_histogram_property_fn!(double, f64, "DoubleLinearHistogramProperty", linear);
bench_histogram_property_fn!(int, i64, "IntExponentialHistogramProperty", exponential);
bench_histogram_property_fn!(uint, u64, "UintExponentialHistogramProperty", exponential);
bench_histogram_property_fn!(double, f64, "DoubleExponentialHistogramProperty", exponential);

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

    let mut bench = criterion::Benchmark::new("Inspector/new", |b| {
        b.iter_with_large_drop(|| Inspector::new());
    });
    bench = bench.with_function("Inspector/root", |b| {
        b.iter_with_large_setup(
            || Inspector::new(),
            |inspector| {
                inspector.root();
            },
        );
    });
    bench = node_benchmarks(bench);
    bench = int_property_benchmarks(bench);
    bench = uint_property_benchmarks(bench);
    bench = double_property_benchmarks(bench);
    for prop_size in &[4, 8, 100, 2000, 2048, 10000] {
        bench = string_property_benchmarks(bench, *prop_size);
        bench = bytes_property_benchmarks(bench, *prop_size);
    }
    for array_size in &[32, 128, 240] {
        bench = int_array_property_benchmarks(bench, *array_size);
        bench = uint_array_property_benchmarks(bench, *array_size);
        bench = double_array_property_benchmarks(bench, *array_size);
        bench = string_array_property_benchmarks(bench, *array_size);

        bench = int_linear_histogram_property_benchmarks(bench, *array_size);
        bench = uint_linear_histogram_property_benchmarks(bench, *array_size);
        bench = double_linear_histogram_property_benchmarks(bench, *array_size);

        bench = int_exponential_histogram_property_benchmarks(bench, *array_size);
        bench = uint_exponential_histogram_property_benchmarks(bench, *array_size);
        bench = double_exponential_histogram_property_benchmarks(bench, *array_size);
    }

    bench = bench_heap_extend(bench);
    bench = bench_write_after_tree_cow_read(bench);

    c.bench("fuchsia.rust_inspect.benchmarks", bench);
}
