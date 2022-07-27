// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Error,
    argh::{FromArgValue, FromArgs},
    bench_utils::{generate_selectors_till_level, parse_selectors, InspectHierarchyGenerator},
    fidl_fuchsia_inspect::{TreeMarker, TreeProxy},
    fuchsia_inspect::{
        hierarchy::filter_hierarchy, reader::ReadableTree, testing::DiagnosticsHierarchyGetter,
        ArithmeticArrayProperty, ArrayProperty, ExponentialHistogramParams, Heap,
        HistogramProperty, Inspector, LinearHistogramParams, Node, NumericProperty, Property,
    },
    fuchsia_trace as ftrace,
    fuchsia_trace_provider::trace_provider_create_with_fdio,
    inspect_runtime::service::{spawn_tree_server, TreeServerSettings},
    lazy_static::lazy_static,
    mapped_vmo::Mapping,
    num::{pow, traits::FromPrimitive, One},
    paste,
    rand::{rngs::StdRng, SeedableRng},
    std::{
        ops::{Add, Mul},
        sync::Arc,
    },
};

mod bench_utils;

lazy_static! {
    static ref SELECTOR_TILL_LEVEL_30: Vec<String> = generate_selectors_till_level(30);
}

const HIERARCHY_GENERATOR_SEED: u64 = 0;

#[derive(FromArgs)]
#[argh(description = "Rust inspect benchmarks")]
struct Args {
    #[argh(option, description = "number of iterations", short = 'i', default = "500")]
    iterations: usize,

    #[argh(option, description = "which benchmark to run (writer, selector)")]
    benchmark: BenchmarkOption,
}

enum BenchmarkOption {
    Writer,
    Selector,
}

impl FromArgValue for BenchmarkOption {
    fn from_arg_value(value: &str) -> Result<Self, String> {
        match value {
            "writer" => Ok(BenchmarkOption::Writer),
            "selector" => Ok(BenchmarkOption::Selector),
            _ => Err(format!("Unknown benchmark \"{}\"", value)),
        }
    }
}

// Generate a function to benchmark inspect hierarchy filtering.
// The benchmark takes a snapshot of a seedable randomly generated
// inspect hierarchy in a vmo and then applies the given selectors
// to the snapshot to filter it down.
macro_rules! generate_selector_benchmark_fn {
    ($name: expr, $size: expr, $label:expr, $selectors: expr) => {
        paste::paste! {
            fn [<$name _ $size>](iterations: usize) {
                let inspector = Inspector::new();
                let mut hierarchy_generator = InspectHierarchyGenerator::new(
                    StdRng::seed_from_u64(HIERARCHY_GENERATOR_SEED), inspector);
                hierarchy_generator.generate_hierarchy($size);
                let hierarchy_matcher = parse_selectors(&$selectors);

                for _ in 0..iterations {
                // Trace format is <label>/<size>
                ftrace::duration!(
                    "benchmark",
                    concat!(
                        $label,
                        "/",
                        stringify!($size),
                    )
                );

                let hierarchy = hierarchy_generator.get_diagnostics_hierarchy().into_owned();
                let _ = filter_hierarchy(hierarchy, &hierarchy_matcher)
                    .expect("Unable to filter hierarchy.");
                }

            }
        }
    };
}

macro_rules! generate_selector_benchmarks {
    ($name: expr, $label: expr, $selectors: expr, [$($size: expr),*]) => {
        $(
            generate_selector_benchmark_fn!(
                $name,
                $size,
                $label,
                $selectors
            );

        )*
    };
}

generate_selector_benchmarks!(
    bench_snapshot_and_select,
    "SnapshotAndSelect",
    SELECTOR_TILL_LEVEL_30,
    [10, 100, 1000, 10000, 100000]
);

fn selector_benchmark(iterations: usize) {
    bench_snapshot_and_select_10(iterations);
    bench_snapshot_and_select_100(iterations);
    bench_snapshot_and_select_1000(iterations);
    bench_snapshot_and_select_10000(iterations);
    bench_snapshot_and_select_100000(iterations);
}

const NAME: &str = "name";

macro_rules! bench_numeric_property_fn {
    ($name:ident, $type:ty, $Property:expr) => {
        paste::paste! {
            fn [<bench_ $name _property>](root: &Node) {
                let property = {
                    ftrace::duration!("benchmark",
                                      concat!("Node::create_", stringify!($name), "_property"));
                    root.[<create_ $name>](NAME, 0 as $type)
                };
                {
                    ftrace::duration!("benchmark", concat!($Property, "::set"));
                    property.set(1 as $type);
                }
                {
                    ftrace::duration!("benchmark", concat!($Property, "::add"));
                    property.add(1 as $type);
                }
                {
                    ftrace::duration!("benchmark", concat!($Property, "::subtract"));
                    property.subtract(1 as $type);
                }
                {
                    ftrace::duration!("benchmark", concat!($Property, "::drop"));
                    drop(property);
                }
                {
                    ftrace::duration!(
                        "benchmark",
                        concat!("Node::record_", stringify!($name), "_property"));
                    root.[<record_ $name>](NAME, 0 as $type);
                }
            }
        }
    };
}

macro_rules! bench_arithmetic_array_fn_impls {
    ($name:ident, $type:ty, $Array:expr, [$($size:expr),*]) => {
        $(
            paste::paste! {
                fn [<bench_ $name _array_ $size>](root: &Node, iteration: usize) {
                    let index = iteration % $size;
                    let array = {
                        ftrace::duration!(
                            "benchmark",
                            concat!("Node::create_", stringify!($name), "_array/", $size));
                        root.[<create_ $name _array>](NAME, $size)
                    };
                    {
                        ftrace::duration!("benchmark", concat!($Array, "::set/", $size));
                        array.set(index, 1 as $type);
                    }
                    {
                        ftrace::duration!("benchmark", concat!($Array, "::add/", $size));
                        array.add(index, 1 as $type);
                    }
                    {
                        ftrace::duration!("benchmark", concat!($Array, "::subtract/", $size));
                        array.subtract(index, 1 as $type);
                    }
                    {
                        ftrace::duration!("benchmark", concat!($Array, "::drop/", $size));
                        drop(array);
                    }
               }
            }
        )*
    };
}

macro_rules! bench_arithmetic_array_fns {
    ($name:ident, $type:ty, $Array:expr) => {
        bench_arithmetic_array_fn_impls!($name, $type, $Array, [32, 128, 240]);
    };
}

macro_rules! bench_string_array_fns {
    ([$($size:expr),*]) => {
        $(
            paste::paste! {
                fn [<bench_string_array_ $size>](root: &Node, iteration: usize) {
                    let index = iteration % $size;
                    let array = {
                        ftrace::duration!("benchmark", concat!("Node::create_string_array/", $size));
                        root.create_string_array(NAME, $size)
                    };

                    {
                        ftrace::duration!("benchmark", concat!("StringArrayProperty::set/", $size));
                        array.set(index, "one");
                    }

                    {
                        ftrace::duration!("benchmark", concat!("StringArrayProperty::clear/", $size));
                        array.clear();
                    }

                    {
                        ftrace::duration!("benchmark", concat!("StringArrayProperty::drop/", $size));
                        drop(array);
                    }
                }
            }
        )*
    };
}

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

macro_rules! bench_histogram_fn_impls {
    ($name:ident, $type:ty, $Histogram:expr, $histogram_type:ident, [$($size:expr),*]) => {
        $(
            paste::paste! {
                fn [<bench_ $name _ $histogram_type _histogram_ $size>](root: &Node) {
                    let (params, value) = [<get_ $histogram_type _bench_data>]($size);
                    let histogram = {
                        ftrace::duration!(
                            "benchmark",
                            concat!("Node::create_",
                                    stringify!($name), "_", stringify!($histogram_type),
                                    "_histogram/", $size));
                        root.[<create_ $name _ $histogram_type _histogram>](NAME, params)
                    };
                    {
                        ftrace::duration!("benchmark", concat!($Histogram, "::insert/", $size));
                        histogram.insert(value);
                    }
                    {
                        ftrace::duration!("benchmark",
                                          concat!($Histogram, "::insert_overflow/", $size));
                        histogram.insert(10000000 as $type);
                    }
                    {
                        ftrace::duration!("benchmark",
                                          concat!($Histogram, "::insert_underflow/", $size));
                        histogram.insert(0 as $type);
                    }
                    {
                        ftrace::duration!("benchmark",
                                          concat!($Histogram, "::drop/", $size));
                        drop(histogram);
                    }
                }
            }
        )*
    };
}

macro_rules! bench_histogram_fn {
    ($name:ident, $type:ty, $Histogram:expr, $histogram_type:ident) => {
        bench_histogram_fn_impls!($name, $type, $Histogram, $histogram_type, [32, 128, 240]);
    };
}

fn get_string_value(size: usize) -> String {
    std::iter::repeat("a").take(size).collect::<String>()
}

fn get_bytes_value(size: usize) -> Vec<u8> {
    vec![1; size]
}

macro_rules! bench_property_fn_impl {
    ($name:ident, $Property:expr, [$($size:expr),*]) => {
        $(
            paste::paste! {
                fn [<bench_ $name _property_ $size>](root: &Node) {
                    let initial_value = [<get_ $name _value>](0);
                    let value = [<get_ $name _value>]($size);
                    let property = {
                        ftrace::duration!(
                            "benchmark",
                            concat!("Node::create_", stringify!($name), "/", $size));
                        root.[<create_ $name>](NAME, &initial_value)
                    };
                    {
                        ftrace::duration!("benchmark", concat!($Property, "::set/", $size));
                        property.set(&value);
                    }
                    {
                        ftrace::duration!("benchmark",
                                          concat!($Property, "::set_again/", $size));
                        property.set(&value);
                    }
                    {
                        ftrace::duration!("benchmark",
                                          concat!($Property, "::drop/", $size));
                        drop(property);
                    }
                    {
                        ftrace::duration!(
                            "benchmark",
                            concat!("Node::record_", stringify!($name), "/", $size));
                        root.[<record_ $name>](NAME, &initial_value);
                    };
                }
            }
        )*
    };
}

macro_rules! bench_property_fn {
    ($name:ident, $Property:expr) => {
        bench_property_fn_impl!($name, $Property, [4, 8, 100, 2000, 2048, 10000]);
    };
}

bench_numeric_property_fn!(int, i64, "IntProperty");
bench_numeric_property_fn!(uint, u64, "UintProperty");
bench_numeric_property_fn!(double, f64, "DoubleProperty");

bench_arithmetic_array_fns!(int, i64, "IntArrayProperty");
bench_arithmetic_array_fns!(uint, u64, "UintArrayProperty");
bench_arithmetic_array_fns!(double, f64, "DoubleArrayProperty");

bench_string_array_fns!([32, 128, 240]);

bench_histogram_fn!(int, i64, "IntLinearHistogramProperty", linear);
bench_histogram_fn!(uint, u64, "UintLinearHistogramProperty", linear);
bench_histogram_fn!(double, f64, "DoubleLinearHistogramProperty", linear);
bench_histogram_fn!(int, i64, "IntExponentialHistogramProperty", exponential);
bench_histogram_fn!(uint, u64, "UintExponentialHistogramProperty", exponential);
bench_histogram_fn!(double, f64, "DoubleExponentialHistogramProperty", exponential);

bench_property_fn!(string, "StringProperty");
bench_property_fn!(bytes, "BytesProperty");

fn create_inspector() -> Inspector {
    ftrace::duration!("benchmark", "Inspector::new");
    Inspector::new()
}

fn get_root(inspector: &Inspector) -> &Node {
    ftrace::duration!("benchmark", "Inspector::root");
    inspector.root()
}

fn bench_node(root: &Node) {
    let node = {
        ftrace::duration!("benchmark", "Node::create_child");
        root.create_child(NAME)
    };
    {
        ftrace::duration!("benchmark", "Node::drop");
        drop(node);
    }
}

fn bench_heap_extend() {
    let (mapping, _) = {
        ftrace::duration!("benchmark", "Heap::create_1mb_vmo");
        Mapping::allocate(1 << 21)
    }
    .unwrap();
    let mut heap = Heap::new(Arc::new(mapping)).unwrap();
    let mut blocks = Vec::with_capacity(513);
    {
        ftrace::duration!("benchmark", "Heap::allocate_512k");
        for _ in 0..512 {
            blocks.push(heap.allocate_block(2048).unwrap());
        }
    }
    {
        ftrace::duration!("benchmark", "Heap::extend");
        blocks.push(heap.allocate_block(2048).unwrap());
    }
    {
        ftrace::duration!("benchmark", "Heap::free");
        for block in blocks {
            heap.free_block(block).unwrap();
        }
    }
    {
        ftrace::duration!("benchmark", "Heap::drop");
        drop(heap);
    }
}

// Benchmark the write-speed of a local inspector after it has been copy-on-write
// served over FIDL
fn single_iteration_fuchsia_inspect_tree() {
    let inspector = create_inspector();

    let mut nodes = vec![];

    for i in 0..1015 {
        nodes.push(inspector.root().create_int("i", i));
    }

    let proxy = spawn_server(inspector.clone()).unwrap();
    // Force TLB shootdown for following writes on the local inspector
    let _ = proxy.vmo();

    ftrace::duration!("benchmark", "Node::IntProperty::CoW::Add");
    for i in nodes {
        i.add(1);
    }
}

macro_rules! single_iteration_fn {
    (array_sizes: [$($array_size:expr),*], property_sizes: [$($prop_size:expr),*]) => {
        paste::paste! {
            fn single_iteration(iteration: usize) {
                let inspector = create_inspector();
                let root = get_root(&inspector);
                bench_node(&root);
                bench_int_property(&root);
                bench_double_property(&root);
                bench_uint_property(&root);

                $(
                    [<bench_string_property_ $prop_size>](&root);
                    [<bench_bytes_property_ $prop_size>](&root);
                )*
                $(
                    [<bench_int_array_ $array_size>](&root, iteration);
                    [<bench_double_array_ $array_size>](&root, iteration);
                    [<bench_uint_array_ $array_size>](&root, iteration);
                    [<bench_string_array_ $array_size>](&root, iteration);

                    [<bench_int_linear_histogram_ $array_size>](&root);
                    [<bench_uint_linear_histogram_ $array_size>](&root);
                    [<bench_double_linear_histogram_ $array_size>](&root);

                    [<bench_int_exponential_histogram_ $array_size>](&root);
                    [<bench_uint_exponential_histogram_ $array_size>](&root);
                    [<bench_double_exponential_histogram_ $array_size>](&root);
                )*
                bench_heap_extend();
            }
        }
    };
}

single_iteration_fn!(array_sizes: [32, 128, 240], property_sizes: [4, 8, 100, 2000, 2048, 10000]);

fn spawn_server(inspector: Inspector) -> Result<TreeProxy, Error> {
    let (tree, request_stream) = fidl::endpoints::create_proxy_and_stream::<TreeMarker>()?;
    spawn_tree_server(inspector, TreeServerSettings::default(), request_stream);
    Ok(tree)
}

fn writer_benchmark(iterations: usize) {
    for i in 0..iterations {
        single_iteration(i);
        single_iteration_fuchsia_inspect_tree();
    }
}

#[fuchsia::main]
async fn main() -> Result<(), Error> {
    trace_provider_create_with_fdio();
    let args: Args = argh::from_env();

    match args.benchmark {
        BenchmarkOption::Writer => {
            writer_benchmark(args.iterations);
        }
        BenchmarkOption::Selector => {
            selector_benchmark(args.iterations);
        }
    }

    Ok(())
}
