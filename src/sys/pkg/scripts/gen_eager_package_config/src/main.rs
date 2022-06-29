// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    lib::{
        generate_omaha_client_config, generate_pkg_resolver_config, Args, InputConfig,
        PublicKeysByServiceUrl,
    },
    std::{fs::File, io::BufReader},
};

fn main_inner(args: &Args) -> Result<(), anyhow::Error> {
    let key_config: PublicKeysByServiceUrl =
        serde_json::from_reader(BufReader::new(File::open(&args.key_config_file)?))?;

    let input_configs: Vec<InputConfig> = args
        .eager_package_config_files
        .iter()
        .map(|f| File::open(f).expect(&format!("opening file {:?}", f)))
        .map(|e| serde_json::from_reader(BufReader::new(e)).unwrap())
        .collect();

    let omaha_client_config = generate_omaha_client_config(&input_configs, &key_config);
    let pkg_resolver_config = generate_pkg_resolver_config(&input_configs, &key_config);

    serde_json::to_writer(&File::create(&args.out_omaha_client_config)?, &omaha_client_config)?;
    serde_json::to_writer(&File::create(&args.out_pkg_resolver_config)?, &pkg_resolver_config)?;

    Ok(())
}

fn main() -> Result<(), anyhow::Error> {
    let args: Args = argh::from_env();
    main_inner(&args)
}
