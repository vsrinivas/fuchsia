// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

use {
    failure::{Error, ResultExt},
    fidl_fuchsia_diagnostics_inspect::{
        DisplaySettings, FormatSettings, ReaderMarker, ReaderSelector, TextSettings,
    },
    fuchsia_async as fasync,
    fuchsia_component::client::{launcher, AppBuilder},
    serde_json::json,
    std::{
        fs::{create_dir, create_dir_all, write, File},
        path::Path,
    },
};

const ARCHIVIST_URL: &str =
    "fuchsia-pkg://fuchsia.com/archivist_data_stats_test#meta/observer_with_data_stats.cmx";
const ALL_SELECTORS: &[u8] = include_bytes!("../config/all_selectors.txt");
const ARCHIVIST_CONFIG: &[u8] = include_bytes!("../config/observer_config.json");

#[fasync::run_singlethreaded]
async fn main() -> Result<(), Error> {
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

    let expected = json!([{
        "path": "/out/test_data",
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
    }]);

    let launcher = launcher()?;
    let mut archivist = AppBuilder::new(ARCHIVIST_URL)
        .add_dir_to_namespace("/config/data".into(), File::open(config_path)?)?
        .add_dir_to_namespace("/data/archive".into(), File::open(archive_path)?)?
        .add_dir_to_namespace("/test_data".into(), File::open(test_data_path)?)?
        .spawn(&launcher)?;

    let reader = archivist.connect_to_service::<ReaderMarker>()?;
    let mut string_selector = ReaderSelector::StringSelector("**:**:*".into());
    reader.add_selector(&mut string_selector).await?.expect("adding selector");

    let mut settings = FormatSettings::empty();
    settings.format = Some(DisplaySettings::Text(TextSettings { indent: 2 }));

    let json_buffer = reader
        .format(settings)
        .await
        .context("requesting format")?
        .expect("should have been trivial to format");
    let mut byte_buf = vec![0u8; json_buffer.size as usize];
    json_buffer.vmo.read(&mut byte_buf, 0)?;

    archivist.kill()?;
    let _ = archivist.wait().await?;

    let output: serde_json::Value = serde_json::from_slice(&byte_buf)?;
    assert_eq!(expected, output, "JSON produced must match expected output");

    Ok(())
}
