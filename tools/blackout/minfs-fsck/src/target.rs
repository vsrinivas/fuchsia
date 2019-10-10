// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    blackout_target::{generate_content, generate_name, CommonCommand, CommonOpts},
    failure::{Error, ResultExt},
    fs_management::{Filesystem, Minfs},
    std::{
        fs::File,
        io::{Read, Write},
    },
    structopt::StructOpt,
};

#[derive(Debug, StructOpt)]
#[structopt(rename_all = "kebab-case")]
struct Opts {
    #[structopt(flatten)]
    common: CommonOpts,
    /// A particular step of the test to perform.
    #[structopt(subcommand)]
    commands: CommonCommand,
}

fn setup(minfs: Filesystem<Minfs>) -> Result<(), Error> {
    println!("formatting block device with minfs");
    minfs.format().context("failed to format minfs")?;

    Ok(())
}

fn test(mut minfs: Filesystem<Minfs>, seed: u64) -> Result<(), Error> {
    let root = format!("/test-fs-root-{}", seed);

    println!("mounting minfs into default namespace at {}", root);
    minfs.mount(&root).context("failed to mount minfs")?;

    println!("generating load");
    // create a file, write some garbage to it, close it, open it, read it, verify random garbage
    // contents, delete it, rinse, repeat.
    loop {
        let contents = generate_content(seed);
        let file_path = format!("{}/{}", root, generate_name(seed));

        {
            let mut file = File::create(&file_path)?;
            file.write_all(&contents)?;
        }

        {
            let mut file = File::open(&file_path)?;
            let mut read_contents = Vec::new();
            file.read_to_end(&mut read_contents)?;
            assert_eq!(contents, read_contents);
        }

        std::fs::remove_file(&file_path)?;
    }
}

fn verify(minfs: Filesystem<Minfs>) -> Result<(), Error> {
    println!("verifying disk with fsck");
    minfs.fsck().context("failed to run fsck")?;

    println!("verification successful");
    Ok(())
}

fn main() -> Result<(), Error> {
    let opts = Opts::from_args();

    println!("minfs block device: {}", opts.common.block_device);
    let minfs = Minfs::new(&opts.common.block_device)?;

    match opts.commands {
        CommonCommand::Setup => setup(minfs),
        CommonCommand::Test => test(minfs, opts.common.seed),
        CommonCommand::Verify => verify(minfs),
    }
}
