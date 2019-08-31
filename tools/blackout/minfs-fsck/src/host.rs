// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    blackout_host::{CommonOpts, Test},
    failure::{bail, Error},
    std::{io::Write, thread::sleep, time::Duration},
    structopt::StructOpt,
};

#[derive(StructOpt)]
#[structopt(rename_all = "kebab-case")]
struct Opts {
    #[structopt(flatten)]
    common: CommonOpts,
}

fn main() -> Result<(), Error> {
    let opt = Opts::from_args();

    let test = Test::new(
        "run fuchsia-pkg://fuchsia.com/minfs-fsck-target#meta/minfs_fsck_target.cmx",
        opt.common,
    )
    .expect("failed to initialize test");

    println!("{:#?}", test);

    println!("running test...");
    let mut child = test.run_spawn("test")?;

    sleep(Duration::from_secs(5));

    // make sure child process is still running
    if let Ok(Some(_)) = child.try_wait() {
        let out = child.wait_with_output().expect("failed to wait for child process");
        println!("stdout:");
        std::io::stdout().write_all(&out.stdout)?;
        println!("stderr:");
        std::io::stdout().write_all(&out.stderr)?;

        bail!("failed to run command: {}", out.status);
    }

    println!("soft rebooting device...");
    test.soft_reboot().expect("failed to reboot device");

    sleep(Duration::from_secs(30));

    println!("verifying device...");
    let out = test.run_output("verify").expect("failed to verify device");
    if !out.status.success() {
        println!("stdout:\n{}", String::from_utf8(out.stdout)?);
        println!("stderr:\n{}", String::from_utf8(out.stderr)?);

        bail!("failed to run command: {}", out.status);
    }

    println!("verification successful.");

    Ok(())
}
