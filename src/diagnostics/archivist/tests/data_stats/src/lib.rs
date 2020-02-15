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
    "fuchsia-pkg://fuchsia.com/archivist_integration_tests#meta/observer_with_data_stats.cmx";
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
        "test_data": {
            "stats": {
                // NOTE(adamperry): this is due to a lack of ADMIN rights on the test_data dir
                "error": "Query failed"
            },
            "test_data": {
                "size": 34,
                "first": {
                    "size": 5
                },
                "second": {
                    "size": 6
                },
                "third": {
                    "size": 23,
                    "fifth": {
                        "size": 11
                    },
                    "fourth": {
                        "size": 12
                    }
                }
            }
        }
    });

    let launcher = launcher()?;
    let archivist = AppBuilder::new(ARCHIVIST_URL)
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

                if first_result.len() < 1 {
                    continue;
                }

                assert_eq!(first_result.len(), 1);

                let entry = &first_result[0];
                let mem_buf = match entry {
                    fidl_fuchsia_diagnostics::FormattedContent::Json(buffer) => buffer,
                    _ => panic!("should be json formatted text"),
                };

                let mut byte_buf = vec![0u8; mem_buf.size as usize];

                let first_batch_result = mem_buf
                    .vmo
                    .read(&mut byte_buf, 0)
                    .map_err(|_| format_err!("Reading from vmo failed."))
                    .and_then(|_| {
                        std::str::from_utf8(&byte_buf).map_err(|_| {
                            format_err!("Parsing byte vector to utf8 string failed...")
                        })
                    })
                    .map(|s| s.to_string())
                    .unwrap_or(r#"{}"#.to_string());

                let output: serde_json::Value = serde_json::from_str(&first_batch_result)
                    .expect("should have valid json from the first payload.");

                // TODO(fxb/43112): when the archivist outputs the new JSON format for which we
                // have a deserializer, perform an `assert_inspect_tree` that checks the other
                // values in the response as well, not only data stats.
                if let Some(data_stats) = output
                    .get("contents")
                    .and_then(|contents| contents.get("root"))
                    .and_then(|root| root.get("data_stats"))
                {
                    assert_eq!(*data_stats, expected);
                    return;
                }
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
