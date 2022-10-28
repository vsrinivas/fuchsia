// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![warn(clippy::all)]

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
        .map(|f| File::open(f).unwrap_or_else(|_| panic!("opening file {:?}", f)))
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

#[cfg(test)]
mod tests {
    use super::*;
    use lib::test_support;

    #[test]
    fn test_config_ok() {
        use eager_package_config::omaha_client::EagerPackageConfigsJson as OmahaConfigsJson;
        use eager_package_config::pkg_resolver::EagerPackageConfigs as ResolverConfigs;
        use tempfile::NamedTempFile;

        let omaha_out = NamedTempFile::new().unwrap();
        let pkgresolver_out = NamedTempFile::new().unwrap();

        let keyconfig_file = NamedTempFile::new().unwrap();
        serde_json::to_writer(&keyconfig_file, &test_support::make_key_config_for_test()).unwrap();

        let input_files: Vec<NamedTempFile> = test_support::make_configs_for_test()
            .iter()
            .map(|config| {
                let tempfile = NamedTempFile::new().unwrap();
                serde_json::to_writer(&tempfile, &config).unwrap();
                tempfile
            })
            .collect();

        let location_of = |f: &NamedTempFile| f.path().to_str().unwrap().to_string();

        main_inner(&Args {
            out_omaha_client_config: location_of(&omaha_out),
            out_pkg_resolver_config: location_of(&pkgresolver_out),
            key_config_file: location_of(&keyconfig_file),
            eager_package_config_files: input_files.iter().map(|file| location_of(file)).collect(),
        })
        .unwrap();

        // assert that both generated configs are valid and parseable.
        let _: OmahaConfigsJson = serde_json::from_reader(BufReader::new(omaha_out)).unwrap();
        let _: ResolverConfigs = serde_json::from_reader(BufReader::new(pkgresolver_out)).unwrap();
    }
}
