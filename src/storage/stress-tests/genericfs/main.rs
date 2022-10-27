// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

mod deletion_actor;
mod environment;
mod file_actor;
mod instance_actor;

use {
    argh::FromArgs,
    environment::FsEnvironment,
    fidl::endpoints::Proxy,
    fidl_fuchsia_fxfs::{CryptManagementMarker, CryptMarker, KeyPurpose},
    fs_management::{F2fs, Fxfs, Minfs},
    fuchsia_async as fasync,
    fuchsia_component::client::connect_to_protocol,
    fuchsia_zircon as zx,
    std::sync::Arc,
    stress_test::run_test,
    tracing::Level,
};

#[derive(Clone, Debug, FromArgs)]
/// Creates an instance of fvm and performs stressful operations on it
pub struct Args {
    /// seed to use for this stressor instance
    #[argh(option, short = 's')]
    seed: Option<u64>,

    /// number of operations to complete before exiting.
    #[argh(option, short = 'o')]
    num_operations: Option<u64>,

    /// filter logging by level (off, error, warn, info, debug, trace)
    #[argh(option, short = 'l')]
    log_filter: Option<Level>,

    /// size of one block of the ramdisk (in bytes)
    #[argh(option, default = "512")]
    ramdisk_block_size: u64,

    /// number of blocks in the ramdisk
    /// defaults to 106MiB ramdisk
    #[argh(option, default = "217088")]
    ramdisk_block_count: u64,

    /// size of one slice in FVM (in bytes)
    #[argh(option, default = "32768")]
    fvm_slice_size: u64,

    /// controls how often blobfs is killed and the ramdisk is unbound
    #[argh(option, short = 'd')]
    disconnect_secs: Option<u64>,

    /// if set, the test runs for this time limit before exiting successfully.
    #[argh(option, short = 't')]
    time_limit_secs: Option<u64>,

    /// which filesystem to target (e.g. 'fxfs' or 'minfs').
    #[argh(option, short = 'f')]
    target_filesystem: String,

    /// parameter passed in by rust test runner
    #[argh(switch)]
    // TODO(fxbug.dev/84729)
    #[allow(unused)]
    nocapture: bool,
}

#[fasync::run_singlethreaded(test)]
async fn test() {
    let args: Args = argh::from_env();

    match args.log_filter {
        Some(filter) => diagnostics_log::init!(&[], diagnostics_log::interest(filter)),
        None => diagnostics_log::init!(),
    }

    match args.target_filesystem.as_str() {
        "fxfs" => {
            let crypt_management_service = connect_to_protocol::<CryptManagementMarker>()
                .expect("Failed to connect to the crypt management service");
            let mut key = [0; 32];
            zx::cprng_draw(&mut key);
            crypt_management_service
                .add_wrapping_key(0, &key)
                .await
                .expect("FIDL failed")
                .expect("add_wrapping_key failed");
            zx::cprng_draw(&mut key);
            crypt_management_service
                .add_wrapping_key(1, &key)
                .await
                .expect("FIDL failed")
                .expect("add_wrapping_key failed");
            crypt_management_service
                .set_active_key(KeyPurpose::Data, 0)
                .await
                .expect("FIDL failed")
                .expect("set_active_key failed");
            crypt_management_service
                .set_active_key(KeyPurpose::Metadata, 1)
                .await
                .expect("FIDL failed")
                .expect("set_active_key failed");
            let env = FsEnvironment::new(
                Fxfs::with_crypt_client(Arc::new(|| {
                    connect_to_protocol::<CryptMarker>()
                        .expect("Failed to connect to crypt service")
                        .into_channel()
                        .expect("Unable to get channel")
                        .into()
                })),
                args,
            )
            .await;
            run_test(env).await;
        }
        "minfs" => {
            let env = FsEnvironment::new(Minfs::default(), args).await;
            run_test(env).await;
        }
        "f2fs" => {
            let env = FsEnvironment::new(F2fs::default(), args).await;
            run_test(env).await;
        }
        _ => panic!("Unsupported filesystem {}", args.target_filesystem),
    }
}
