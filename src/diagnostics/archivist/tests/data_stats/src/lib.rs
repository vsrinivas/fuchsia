// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
use {
    anyhow::{format_err, Context as _, Error},
    fidl::endpoints::create_proxy,
    fidl_fuchsia_diagnostics::ArchiveMarker,
    fuchsia_async::{self as fasync, DurationExt, TimeoutExt},
    fuchsia_component::client::{launcher, AppBuilder},
    fuchsia_zircon::DurationNum,
    serde_json::json,
    std::{
        fs::{create_dir, create_dir_all, write, File},
        path::Path,
    },
};

const ARCHIVIST_URL: &str =
    "fuchsia-pkg://fuchsia.com/archivist_data_stats_tests#meta/observer_with_data_stats.cmx";
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

    let expected = json!({
        "path": "observer_with_data_stats.cmx",
        "contents": {
            "root": {
                "stats": {
                    // NOTE(adamperry): this is due to a lack of ADMIN rights on the test_data dir
                    "error": "Query failed",
                },
                "test_data": {
                    "size": 34,
                    "first": {
                        "size": 5,
                    },
                    "second": {
                        "size": 6,
                    },
                    "third": {
                        "size": 23,
                        "fifth": {
                            "size": 11,
                        },
                        "fourth": {
                            "size": 12,
                        },
                    },
                },
            },
        },
    });

    let launcher = launcher()?;
    let mut archivist = AppBuilder::new(ARCHIVIST_URL)
        .add_dir_to_namespace("/config/data".into(), File::open(config_path)?)?
        .add_dir_to_namespace("/data/archive".into(), File::open(archive_path)?)?
        .add_dir_to_namespace("/test_data".into(), File::open(test_data_path)?)?
        .spawn(&launcher)?;
    let archive_accessor = archivist.connect_to_service::<ArchiveMarker>().unwrap();
    let (reader_consumer, reader_server) = create_proxy().unwrap();
    archive_accessor
        .read_inspect(reader_server, &mut Vec::new().into_iter())
        .await
        .context("setting up the reader server")
        .expect("fidl channels should be fine")
        .expect("setting up the server shouldn't have any issues.");

    let retrieve_first_real_result = || {
        async move {
            loop {
                let (batch_consumer, batch_server) = create_proxy().unwrap();
                reader_consumer
                    .get_snapshot(fidl_fuchsia_diagnostics::Format::Json, batch_server)
                    .await
                    .context("requesting format")
                    .expect("fidl should be fine")
                    .expect("should have been trivial to format");

                let first_result = batch_consumer
                    .get_next()
                    .await
                    .context("retrieving first batch of hierarchy data")
                    .expect("fidl should be fine")
                    .expect("expect batches to be retrieved without error");

                if first_result.len() < 2 {
                    continue;
                }

                let first_batch_results = first_result
                    .iter()
                    .map(|entry| {
                        let mem_buf = match entry {
                            fidl_fuchsia_diagnostics::FormattedContent::FormattedJsonHierarchy(
                                buffer,
                            ) => buffer,
                            _ => panic!("should be json formatted text"),
                        };

                        let mut byte_buf = vec![0u8; mem_buf.size as usize];

                        mem_buf
                            .vmo
                            .read(&mut byte_buf, 0)
                            .map_err(|_| format_err!("Reading from vmo failed."))
                            .and_then(|_| {
                                std::str::from_utf8(&byte_buf).map_err(|_| {
                                    format_err!("Parsing byte vector to utf8 string failed...")
                                })
                            })
                            .map(|s| s.to_string())
                            .unwrap_or(r#"{}"#.to_string())
                    })
                    .collect::<Vec<String>>()
                    .join(",");

                let results = format!("[{}]", first_batch_results);

                archivist.kill().expect("archivist should be killable here.");
                let _ = archivist
                    .wait()
                    .await
                    .expect("waiting on archivist should finish implying it died.");

                let output: serde_json::Value = serde_json::from_str(&results)
                    .expect("should have valid json from the first payload.");

                let hierarchy_array =
                    output.as_array().expect("the formatting should produce an array of values.");

                assert_eq!(
                    hierarchy_array.len(),
                    2,
                    "only test data and health node should be present"
                );

                // the hierarchy array contains a health hierarchy which has a timestamp
                // that we can't pattern match. so just search for the test data.
                let mut test_data_present = false;
                for hierarchy in hierarchy_array {
                    if *hierarchy == expected {
                        test_data_present = true;
                        break;
                    }
                }

                assert!(test_data_present, "expected the test_data hierarchy to be present.");
                // Need explicit return to break the loop and async fn.
                return;
            }
        }
    };
    retrieve_first_real_result()
        .on_timeout(READ_TIMEOUT_SECONDS.seconds().after_now(), || {
            panic!("failed to get meaningful results from reader service.")
        })
        .await;
    Ok(())
}
