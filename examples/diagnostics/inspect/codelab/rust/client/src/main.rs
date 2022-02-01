// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Error,
    fidl_fuchsia_component::BinderMarker,
    fidl_fuchsia_examples_inspect::ReverserMarker,
    fuchsia_component::client,
    fuchsia_zircon::DurationNum,
    tracing::{error, info},
};

struct Args {
    strings: Vec<String>,
}

impl Args {
    fn load() -> Option<Self> {
        let args = std::env::args().into_iter().skip(1).collect::<Vec<String>>();
        if args.len() < 1 {
            return None;
        }

        Some(Args { strings: args.to_vec() })
    }
}

fn usage() -> String {
    let arg0 = std::env::args().next().unwrap_or("inspect_rust_codelab_client".to_string());
    format!(
        "Usage: {:?} <string> [string...]
  string: Strings provided on the command line to reverse",
        arg0
    )
}

#[fuchsia::component(logging_tags = ["inspect_rust_codelab", "client"])]
async fn main() -> Result<(), Error> {
    let args = Args::load().unwrap_or_else(|| {
        error!("Invalid args. {}", usage());
        std::process::exit(0);
    });

    let reverser =
        client::connect_to_childs_protocol::<ReverserMarker>("reverser".to_string(), None).await?;

    // Start FizzBuzz. For the purposes of the codelab.
    let _proxy =
        client::connect_to_childs_protocol::<BinderMarker>("fizzbuzz".to_string(), None).await?;

    // [START reverse_loop]
    for string in args.strings {
        info!("Input: {}", string);
        match reverser.reverse(&string).await {
            Ok(output) => info!("Output: {}", output),
            Err(e) => error!(error = ?e, "Failed to reverse string"),
        }
    }
    // [END reverse_loop]

    info!("Done reversing! Please use `ffx component stop`");

    loop {
        1.seconds().sleep();
    }
}
