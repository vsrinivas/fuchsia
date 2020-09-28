// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Error,
    fidl_fuchsia_diagnostics::ArchiveAccessorMarker,
    fuchsia_async::{self as fasync, DurationExt, TimeoutExt},
    fuchsia_component::client::{launcher, AppBuilder, Stdio},
    fuchsia_inspect::testing::assert_inspect_tree,
    fuchsia_inspect_contrib::reader::{ArchiveReader, Inspect},
    fuchsia_zircon::DurationNum,
    std::{
        fs::{create_dir, create_dir_all, write, File},
        path::Path,
    },
};

const ARCHIVIST_URL: &str =
    "fuchsia-pkg://fuchsia.com/archivist-integration-tests#meta/data_stats_test_archivist.cmx";
const ALL_SELECTORS: &[u8] = include_bytes!("../config/all_selectors.txt");
const ARCHIVIST_CONFIG: &[u8] = include_bytes!("../config/observer_config.json");

// Number of seconds to wait before timing out polling the reader for pumped results.
static READ_TIMEOUT_SECONDS: i64 = 5;

#[fasync::run_singlethreaded(test)]
async fn data_stats() -> Result<(), Error> {
    let config_path = Path::new("/tmp/config/data");
    let selectors_path = config_path.join("pipelines/all/all_selectors.txt");
    let archivist_config_path = config_path.join("observer_config.json");
    let archive_path = "/tmp/archive";
    let test_data_path = "/tmp/test_data";

    create_dir_all(selectors_path.parent().unwrap())?;
    create_dir(archive_path)?;
    create_dir(test_data_path)?;

    write(selectors_path, ALL_SELECTORS)?;
    write(archivist_config_path, ARCHIVIST_CONFIG)?;

    let to_write = vec!["first", "second", "third/fourth", "third/fifth"];
    for filename in &to_write {
        let full_path = Path::new(test_data_path).join(filename);
        std::fs::create_dir_all(full_path.parent().unwrap())?;
        std::fs::write(full_path, filename.as_bytes())?;
    }

    let launcher = launcher()?;
    let mut archivist = AppBuilder::new(ARCHIVIST_URL)
        .add_dir_to_namespace("/config/data".into(), File::open(config_path)?)?
        .add_dir_to_namespace("/data/archive".into(), File::open(archive_path)?)?
        .add_dir_to_namespace("/test_data".into(), File::open(test_data_path)?)?
        .stdout(Stdio::MakePipe)
        .stderr(Stdio::MakePipe)
        .spawn(&launcher)?;
    let archive_accessor = archivist.connect_to_service::<ArchiveAccessorMarker>().unwrap();

    let results = ArchiveReader::new()
        .with_archive(archive_accessor)
        .add_selector("data_stats_test_archivist.cmx:root/data_stats")
        .snapshot::<Inspect>()
        .await
        .expect("got results");
    assert_eq!(results.len(), 1);
    let hierarchy = results.into_iter().next().unwrap().payload.unwrap();
    assert_inspect_tree!(hierarchy, root: {
        data_stats: {
            test_data: {
                stats: {
                    // NOTE(adamperry): this is due to a lack of ADMIN rights on the test_data dir
                    error: "Query failed"
                },
                test_data: {
                    size: 34u64,
                    first: {
                        size: 5u64
                    },
                    second: {
                        size: 6u64
                    },
                    third: {
                        size: 23u64,
                        fifth: {
                            size: 11u64
                        },
                        fourth: {
                            size: 12u64
                        }
                    }
                }
            }
        }
    });

    archivist.kill().unwrap();
    let output = archivist
        .wait_with_output()
        .on_timeout(READ_TIMEOUT_SECONDS.seconds().after_now(), || {
            panic!("archivist did not terminate.")
        })
        .await
        .unwrap();
    assert_eq!("", String::from_utf8(output.stdout).expect("utf8 stdout"));
    assert_eq!("", String::from_utf8(output.stderr).expect("utf8 stderr"));
    Ok(())
}
