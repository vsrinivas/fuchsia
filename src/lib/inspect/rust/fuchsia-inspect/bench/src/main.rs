// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    argh::FromArgs,
    failure::Error,
    fuchsia_async as fasync,
    fuchsia_inspect::{
        DoubleProperty, Inspector, IntProperty, Node, NumericProperty, Property, UintProperty,
    },
    fuchsia_trace as ftrace,
    fuchsia_trace_provider::trace_provider_create_with_fdio,
    paste,
};

#[derive(FromArgs)]
#[argh(description = "Rust inspect benchmarks")]
struct Args {
    #[argh(option, description = "number of iterations", short = 'i', default = "500")]
    iterations: usize,
}

macro_rules! bench_property_fns {
    ($name:ident, $type:ty, $Property:ident) => {
        paste::item! {
            fn [<create_ $name _property>](parent: &Node) -> $Property {
                ftrace::duration!("benchmark",
                                  concat!("Node::create_", stringify!($name), "_property"));
                parent.[<create_ $name>]("name", 0 as $type)
            }

            fn [<$name _property_add>](property: &$Property) {
                ftrace::duration!("benchmark", concat!(stringify!($Property), "::add"));
                property.add(1 as $type);
            }

            fn [<$name _property_set>](property: &$Property) {
                ftrace::duration!("benchmark", concat!(stringify!($Property), "::set"));
                property.set(1 as $type);
            }

            fn [<$name _property_subtract>](property: &$Property) {
                ftrace::duration!("benchmark", concat!(stringify!($Property), "::subtract"));
                property.subtract(1 as $type);
            }

            fn [<drop_ $name _property>](property: $Property) {
                ftrace::duration!("benchmark", concat!(stringify!($Property), "::drop"));
                drop(property);
            }

            fn [<bench_ $name _property>](root: &Node) {
                let property = [<create_ $name _property>](root);
                [<$name _property_set>](&property);
                [<$name _property_add>](&property);
                [<$name _property_subtract>](&property);
                [<drop_ $name _property>](property);
            }
        }
    };
}

bench_property_fns!(int, i64, IntProperty);
bench_property_fns!(uint, u64, UintProperty);
bench_property_fns!(double, f64, DoubleProperty);

fn create_inspector() -> Inspector {
    ftrace::duration!("benchmark", "Inspector::new");
    Inspector::new()
}

fn get_root(inspector: &Inspector) -> &Node {
    ftrace::duration!("benchmark", "Inspector::root");
    inspector.root()
}

fn single_iteration() {
    let inspector = create_inspector();
    let root = get_root(&inspector);
    bench_int_property(&root);
    bench_double_property(&root);
    bench_uint_property(&root);
}

#[fasync::run_singlethreaded]
async fn main() -> Result<(), Error> {
    trace_provider_create_with_fdio();
    let args: Args = argh::from_env();

    for _ in 0..args.iterations {
        single_iteration();
    }

    Ok(())
}
