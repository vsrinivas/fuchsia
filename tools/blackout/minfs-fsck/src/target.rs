// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    blackout_target::{generate_content, generate_name, CommonCommand, CommonOpts},
    failure::{Error, ResultExt},
    fs_management::Minfs,
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

fn setup(opts: Opts) -> Result<(), Error> {
    let minfs = Minfs::new(&opts.common.block_device)?;

    println!("formatting {} with minfs", opts.common.block_device);
    minfs.format().context("failed to format minfs")?;

    Ok(())
}

fn test(opts: Opts) -> Result<(), Error> {
    let mut minfs = Minfs::new(&opts.common.block_device)?;

    let root = format!("/test-fs-root-{}", opts.common.seed);

    println!("mounting minfs into default namespace at {}", root);
    minfs.mount(&root).context("failed to mount minfs")?;

    println!("generating load");
    // create a file, write some garbage to it, close it, open it, read it, verify random garbage
    // contents, delete it, rinse, repeat.
    loop {
        let contents = generate_content(opts.common.seed);
        let file_path = format!("{}/{}", root, generate_name(opts.common.seed));

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

fn verify(opts: Opts) -> Result<(), Error> {
    let minfs = Minfs::new(&opts.common.block_device)?;

    println!("verifying disk with fsck");
    minfs.fsck().context("failed to run fsck")?;

    println!("verification successful");
    Ok(())
}

fn main() -> Result<(), Error> {
    let opts = Opts::from_args();

    match opts.commands {
        CommonCommand::Setup => setup(opts),
        CommonCommand::Test => test(opts),
        CommonCommand::Verify => verify(opts),
    }
}
