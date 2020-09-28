// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{anyhow, bail, format_err, Context, Error},
    fidl_fuchsia_sys::ComponentControllerEvent,
    fuchsia_async as fasync,
    fuchsia_component::client::{launcher, AppBuilder},
    futures::StreamExt,
    glob::glob,
    lazy_static::lazy_static,
    maplit::hashset,
    std::{
        collections::HashSet,
        fs,
        path::{Path, PathBuf},
    },
};

lazy_static! {
    static ref ARCHIVIST_URL: &'static str =
        "fuchsia-pkg://fuchsia.com/archivist-integration-tests#meta/archive_path_test_archivist.cmx";
    static ref ARCHIVE_PATH: &'static str = "/tmp/archive";
    static ref ARCHIVIST_CONFIG: &'static [u8] = include_bytes!("../configs/archivist_config.json");
    static ref CONFIG_PATH: PathBuf = Path::new("/tmp/config/data").to_path_buf();
    static ref ARCHIVIST_CONFIGURATION_PATH: PathBuf = CONFIG_PATH.join("archivist_config.json");
    static ref SAMPLE_FILE_NAME: &'static str = "sample_file";
}

fn get_hub_path() -> Result<String, Error> {
    let glob_query = "/hub/c/archive_path_test_archivist.cmx/*/out";
    match glob(&glob_query)?.next() {
        Some(found_path_result) => found_path_result
            .map(|p| p.to_string_lossy().to_string())
            .map_err(|e| format_err!("Failed reading out dir: {}", e)),
        None => return Err(anyhow!("Out dir not found")),
    }
}

fn verify_out(hub_out_path: &Path) -> Result<(), Error> {
    assert!(hub_out_path.is_dir());

    let dir_entries = fs::read_dir(&hub_out_path)?.filter_map(Result::ok).collect::<Vec<_>>();
    let expected = hashset! {
        "archive".to_string(), "svc".to_string(), "diagnostics".to_string(),
    };
    assert_eq!(
        expected,
        dir_entries
            .into_iter()
            .map(|entry| entry.file_name().to_string_lossy().to_string())
            .collect::<HashSet<_>>()
    );

    let diagnostics_entries =
        fs::read_dir(hub_out_path.join("diagnostics"))?.filter_map(Result::ok).collect::<Vec<_>>();
    let expected = hashset! {"fuchsia.inspect.Tree".to_string()};
    assert_eq!(
        expected,
        diagnostics_entries
            .into_iter()
            .map(|entry| entry.file_name().to_string_lossy().to_string())
            .collect::<HashSet<_>>()
    );

    let diagnostics_entries =
        fs::read_dir(hub_out_path.join("archive"))?.filter_map(Result::ok).collect::<Vec<_>>();
    assert_eq!(diagnostics_entries.len(), 1);

    Ok(())
}

#[fasync::run_singlethreaded(test)]
async fn out_can_be_read() -> Result<(), Error> {
    fs::create_dir_all(&*CONFIG_PATH).context("create archivist config dir")?;
    fs::create_dir_all(*ARCHIVE_PATH).context("create archive dir")?;
    fs::write(&*ARCHIVIST_CONFIGURATION_PATH, *ARCHIVIST_CONFIG)
        .context("write archivist config")?;

    let archivist = AppBuilder::new(*ARCHIVIST_URL)
        .add_dir_to_namespace("/config/data".into(), fs::File::open(&*CONFIG_PATH)?)
        .context("add /config/data")?
        .add_dir_to_namespace("/data/archive".into(), fs::File::open(*ARCHIVE_PATH)?)
        .context("add /data/archive")?
        .spawn(&launcher()?)
        .context("spawn archivist")?;

    let mut component_stream = archivist.controller().take_event_stream();

    // Wait for Archvist's out to be ready.
    match component_stream
        .next()
        .await
        .expect("component event stream ended before termination event")?
    {
        ComponentControllerEvent::OnDirectoryReady {} => {}
        ComponentControllerEvent::OnTerminated { return_code, termination_reason } => {
            bail!(
                "Component terminated unexpectedly. Code: {}. Reason: {:?}",
                return_code,
                termination_reason
            );
        }
    }

    let hub_out_path_str = get_hub_path().context("get hub path")?;
    let hub_out_path = Path::new(&hub_out_path_str);
    verify_out(&hub_out_path).context("verify - first")?;

    // Verify again to ensure we can continue to read.
    verify_out(&hub_out_path).context("verify - second")?;

    Ok(())
}
