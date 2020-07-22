// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Error, fuchsia_async as fasync, fuchsia_component::server::ServiceFs,
    fuchsia_zircon::DurationNum, futures::StreamExt, inspect_codelab_shared::CodelabEnvironment,
    lazy_static::lazy_static,
};

lazy_static! {
    static ref CHILD_ENVIRONMENT_NAME: String = "codelab".to_string();
}

struct Args {
    part: usize,
    strings: Vec<String>,
}

impl Args {
    fn load() -> Option<Self> {
        let args = std::env::args().into_iter().skip(1).collect::<Vec<String>>();
        if args.len() < 2 {
            return None;
        }

        args[0].parse::<usize>().ok().map(|part| Args { part, strings: args[1..].to_vec() })
    }
}

fn usage() -> String {
    let arg0 = std::env::args().next().unwrap_or("inspect_rust_codelab_client".to_string());
    format!(
        "Usage: {:?} <option> <string> [string...]
  option: The server to run. For example \"1\" for part_1
  string: Strings provided on the command line to reverse",
        arg0
    )
}

#[fasync::run_singlethreaded]
async fn main() -> Result<(), Error> {
    let args = Args::load().unwrap_or_else(|| {
        println!("{}", usage());
        std::process::exit(0);
    });

    let mut fs = ServiceFs::new();
    let mut env = CodelabEnvironment::new(
        fs.create_salted_nested_environment(&CHILD_ENVIRONMENT_NAME)?,
        "inspect_rust_codelab",
        args.part,
    );
    fasync::Task::spawn(fs.collect::<()>()).detach();

    env.launch_fizzbuzz()?;
    let reverser = env.launch_reverser()?;

    // [START reverse_loop]
    for string in args.strings {
        println!("Input: {}", string);
        match reverser.reverse(&string).await {
            Ok(output) => println!("Output: {}", output),
            Err(e) => println!("Failed to reverse string. Error: {:?}", e),
        }
    }
    // [END reverse_loop]

    println!("Done. Press Ctrl+C to exit");
    loop {
        1.seconds().sleep();
    }
}
