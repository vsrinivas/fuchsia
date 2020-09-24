// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{deprecated_fidl_server::*, table::*},
    anyhow::{format_err, Error},
    fuchsia_async as fasync,
    fuchsia_component::server::ServiceFs,
    fuchsia_inspect::{self as inspect, ArrayProperty},
    futures::{FutureExt, StreamExt},
    std::ops::AddAssign,
    structopt::StructOpt,
};

mod deprecated_fidl_server;
mod table;

struct PopulateParams<T> {
    floor: T,
    step: T,
    count: usize,
}

fn populated<H: inspect::HistogramProperty>(histogram: H, params: PopulateParams<H::Type>) -> H
where
    H::Type: AddAssign + Copy,
{
    let mut value = params.floor;
    for _ in 0..params.count {
        histogram.insert(value);
        value += params.step;
    }
    histogram
}

#[derive(Debug, StructOpt)]
#[structopt(
    name = "example",
    about = "Example component to showcase Inspect API objects, including an NxM nested table"
)]
struct Options {
    #[structopt(long)]
    rows: usize,

    #[structopt(long)]
    columns: usize,
}

#[fasync::run_singlethreaded]
async fn main() -> Result<(), Error> {
    let opts = Options::from_args();
    if opts.rows == 0 || opts.columns == 0 {
        Options::clap().print_help()?;
        std::process::exit(1);
    }

    let inspector = inspect::Inspector::new();
    let root = inspector.root();
    assert!(inspector.is_valid());

    reset_unique_names();
    let table_node_name = unique_name("table");
    let example_table =
        Table::new(opts.rows, opts.columns, &table_node_name, root.create_child(&table_node_name));

    let int_array = root.create_int_array(unique_name("array"), 3);
    int_array.set(0, 1);
    int_array.add(1, 10);
    int_array.subtract(2, 3);

    let uint_array = root.create_uint_array(unique_name("array"), 3);
    uint_array.set(0, 1);
    uint_array.add(1, 10);
    uint_array.set(2, 3);
    uint_array.subtract(2, 1);

    let double_array = root.create_double_array(unique_name("array"), 3);
    double_array.set(0, 0.25);
    double_array.add(1, 1.25);
    double_array.subtract(2, 0.75);

    let _int_linear_hist = populated(
        root.create_int_linear_histogram(
            unique_name("histogram"),
            inspect::LinearHistogramParams { floor: -10, step_size: 5, buckets: 3 },
        ),
        PopulateParams { floor: -20, step: 1, count: 40 },
    );
    let _uint_linear_hist = populated(
        root.create_uint_linear_histogram(
            unique_name("histogram"),
            inspect::LinearHistogramParams { floor: 5, step_size: 5, buckets: 3 },
        ),
        PopulateParams { floor: 0, step: 1, count: 40 },
    );
    let _double_linear_hist = populated(
        root.create_double_linear_histogram(
            unique_name("histogram"),
            inspect::LinearHistogramParams { floor: 0.0, step_size: 0.5, buckets: 3 },
        ),
        PopulateParams { floor: -1.0, step: 0.1, count: 40 },
    );

    let _int_exp_hist = populated(
        root.create_int_exponential_histogram(
            unique_name("histogram"),
            inspect::ExponentialHistogramParams {
                floor: -10,
                initial_step: 5,
                step_multiplier: 2,
                buckets: 3,
            },
        ),
        PopulateParams { floor: -20, step: 1, count: 40 },
    );
    let _uint_exp_hist = populated(
        root.create_uint_exponential_histogram(
            unique_name("histogram"),
            inspect::ExponentialHistogramParams {
                floor: 0,
                initial_step: 1,
                step_multiplier: 2,
                buckets: 3,
            },
        ),
        PopulateParams { floor: 0, step: 1, count: 40 },
    );
    let _double_exp_hist = populated(
        root.create_double_exponential_histogram(
            unique_name("histogram"),
            inspect::ExponentialHistogramParams {
                floor: 0.0,
                initial_step: 1.25,
                step_multiplier: 3.0,
                buckets: 3,
            },
        ),
        PopulateParams { floor: -1.0, step: 0.1, count: 40 },
    );

    root.record_lazy_child("lazy-node", || {
        async move {
            let inspector = inspect::Inspector::new();
            inspector.root().record_uint("uint", 3);
            Ok(inspector)
        }
        .boxed()
    });
    root.record_lazy_values("lazy-values", || {
        async move {
            let inspector = inspect::Inspector::new();
            inspector.root().record_double("lazy-double", 3.14);
            Ok(inspector)
        }
        .boxed()
    });

    let mut fs = ServiceFs::new();

    // NOTE: this FIDL service is deprecated and the following *should not* be done.
    // Rust doesn't have a way of writing to the deprecated FIDL service, therefore
    // we read what we wrote to the VMO and provide it through the service for testing
    // purposes.
    let inspector_clone = inspector.clone();
    fs.dir("diagnostics")
        .add_fidl_service(move |stream| {
            spawn_inspect_server(stream, example_table.get_node_object());
        })
        .add_fidl_service(move |stream| {
            // Purely for test purposes. Internally inspect creates a pseudo dir diagnostics and
            // adds it as remote in ServiceFs. However, if we try to add the VMO file and the other
            // service in the ServiceFs, an exception occurs. This is purely a workaround for
            // ServiceFS and for the test purpose. A regular component wouldn't do this. It would
            // just do `inspector.serve(&mut fs);`.
            inspect::service::spawn_tree_server(inspector_clone.clone(), stream);
        });

    // TODO(fxbug.dev/41952): remove when all clients writing VMO files today have been migrated to write
    // to Tree.
    inspector
        .duplicate_vmo()
        .ok_or(format_err!("Failed to duplicate VMO"))
        .and_then(|vmo| {
            let size = vmo.get_size()?;
            fs.dir("diagnostics").add_vmo_file_at(
                "root.inspect",
                vmo,
                0, /* vmo offset */
                size,
            );
            Ok(())
        })
        .unwrap_or_else(|e| {
            eprintln!("Failed to expose vmo. Error: {:?}", e);
        });

    fs.take_and_serve_directory_handle()?;

    Ok(fs.collect().await)
}
