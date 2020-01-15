// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

use {
    anyhow::{bail, format_err, Context, Error},
    difference::assert_diff,
    fidl::endpoints::create_proxy,
    fidl_fuchsia_diagnostics::{ArchiveMarker, SelectorArgument},
    fidl_fuchsia_sys::ComponentControllerEvent,
    fuchsia_async::{self as fasync, DurationExt, TimeoutExt},
    fuchsia_component::client::{launch, launcher},
    fuchsia_component::client::{App, AppBuilder},
    fuchsia_zircon::DurationNum,
    futures::StreamExt,
    lazy_static::lazy_static,
    std::{
        fs::{self, create_dir, create_dir_all, write, File},
        path::{Path, PathBuf},
    },
};

const ARCHIVIST_URL: &str =
    "fuchsia-pkg://fuchsia.com/unified_reader_tests#meta/unified_reader_test_archivist.cmx";
const ARCHIVIST_CONFIG: &[u8] = include_bytes!("../configs/archivist_config.json");
const SERVICE_URL: &str =
    "fuchsia-pkg://fuchsia.com/unified_reader_tests#meta/iquery_example_component.cmx";

const ALL_GOLDEN_JSON: &[u8] = include_bytes!("../configs/all_golden.json");
const SINGLE_VALUE_CLIENT_SELECTOR_JSON: &[u8] =
    include_bytes!("../configs/single_value_client_selector.json");

// Number of seconds to wait before timing out polling the reader for pumped results.
static READ_TIMEOUT_SECONDS: i64 = 10;

static MONIKER_KEY: &str = "path";
static TEST_ARCHIVIST: &str = "unified_reader_test_archivist.cmx";

lazy_static! {
    static ref CONFIG_PATH: PathBuf = Path::new("/tmp/config/data").to_path_buf();
    static ref ARCHIVIST_CONFIGURATION_PATH: PathBuf = CONFIG_PATH.join("archivist_config.json");
    static ref ALL_GOLDEN_JSON_PATH: PathBuf = CONFIG_PATH.join("all_golden.json");
    static ref SINGLE_VALUE_CLIENT_SELECTOR_JSON_PATH: PathBuf =
        CONFIG_PATH.join("single_value_client_selector.json");
    static ref SELECTORS_PATH: PathBuf = CONFIG_PATH.join("pipelines/all/all_selectors.txt");
    static ref ARCHIVE_PATH: &'static str = "/tmp/archive";
    static ref TEST_DATA_PATH: &'static str = "/tmp/test_data";
}

async fn setup_environment() -> Result<(App, App), Error> {
    create_dir_all(&*SELECTORS_PATH.parent().unwrap())?;
    create_dir(*ARCHIVE_PATH)?;
    create_dir(*TEST_DATA_PATH)?;

    write(&*ARCHIVIST_CONFIGURATION_PATH, ARCHIVIST_CONFIG)?;

    write(&*ALL_GOLDEN_JSON_PATH, ALL_GOLDEN_JSON)?;
    write(&*SINGLE_VALUE_CLIENT_SELECTOR_JSON_PATH, SINGLE_VALUE_CLIENT_SELECTOR_JSON)?;

    let to_write = vec!["first", "second", "third/fourth", "third/fifth"];
    for filename in &to_write {
        let full_path = Path::new(*TEST_DATA_PATH).join(filename);
        create_dir_all(full_path.parent().unwrap())?;
        write(full_path, filename.as_bytes())?;
    }

    let archivist = AppBuilder::new(ARCHIVIST_URL)
        .add_dir_to_namespace("/config/data".into(), File::open(&*CONFIG_PATH)?)?
        .add_dir_to_namespace("/data/archive".into(), File::open(*ARCHIVE_PATH)?)?
        .add_dir_to_namespace("/test_data".into(), File::open(*TEST_DATA_PATH)?)?
        .spawn(&launcher()?)?;

    let arguments = vec!["--rows=5".to_string(), "--columns=3".to_string()];

    let example_app = launch(&launcher()?, SERVICE_URL.to_string(), Some(arguments))?;

    let mut component_stream = example_app.controller().take_event_stream();

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
    Ok((archivist, example_app))
}

// Loop indefinitely snapshotting the archive until we get the expected number of
// hierarchies, and then validate that the ordered json represetionation of these hierarchies
// matches the golden file.
async fn retrieve_and_validate_results(
    archivist: &App,
    custom_selectors: Vec<&mut SelectorArgument>,
    golden_file: &PathBuf,
    expected_results_count: usize,
) {
    let archive_accessor = archivist.connect_to_service::<ArchiveMarker>().unwrap();

    let (reader_consumer, reader_server) = create_proxy().unwrap();

    archive_accessor
        .read_inspect(reader_server, &mut custom_selectors.into_iter())
        .await
        .context("setting up the reader server")
        .expect("fidl channels should be fine")
        .expect("setting up the server shouldn't have any issues.");

    loop {
        let (batch_consumer, batch_server) = create_proxy().unwrap();
        reader_consumer
            .get_snapshot(fidl_fuchsia_diagnostics::Format::Json, batch_server)
            .await
            .context("requesting format")
            .expect("fidl channel should be fine.")
            .expect("should have been trivial to format");

        let first_result = batch_consumer
            .get_next()
            .await
            .context("retrieving first batch of hierarchy data")
            .expect("fidl should be fine")
            .expect("expect batches to be retrieved without error");

        if first_result.len() < expected_results_count {
            continue;
        }

        let first_batch_results = first_result
            .iter()
            .map(|entry| {
                let mem_buf = match entry {
                    fidl_fuchsia_diagnostics::FormattedContent::FormattedJsonHierarchy(buffer) => {
                        buffer
                    }
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

        // Convert the results into a json struct so we can split into an array of
        // stringable json values that we can sort.
        let result_json: serde_json::Value =
            serde_json::from_str(&results).expect("Parsing result json failed.");

        let mut string_result_array = result_json
            .as_array()
            .expect("result json is an array of objs.")
            .iter()
            .filter_map(|val| {
                // Filter out the results coming from the archivist so that we don't
                // get timestamped results that we cant golden test.
                // TODO(4601): Do this filtering via selectors when subtree selection
                // is functional.
                val.as_object().map_or(None, |obj| match obj.get(MONIKER_KEY) {
                    Some(serde_json::Value::String(moniker_str)) => {
                        if moniker_str != TEST_ARCHIVIST {
                            Some(
                                serde_json::to_string(&val)
                                    .expect("All entries in the array are valid."),
                            )
                        } else {
                            None
                        }
                    }
                    _ => None,
                })
            })
            .collect::<Vec<String>>();

        string_result_array.sort();

        let sorted_results_json_string = format!("[{}]", string_result_array.join(","));

        let sorted_results_json_struct: serde_json::Value =
            serde_json::from_str(&sorted_results_json_string).unwrap();

        // Convert the json struct into a "pretty" string rather than converting the
        // golden file into a json struct because deserializing the golden file into a
        // struct causes serde_json to convert the u64s into exponential form which
        // causes loss of precision.
        let mut pretty_results = serde_json::to_string_pretty(&sorted_results_json_struct)
            .expect("should be able to format the the results as valid json.");

        let mut expected_string: String =
            fs::read_to_string(golden_file).expect("Reading golden json failed.");

        // Remove whitespace from both strings because text editors will do things like
        // requiring json files end in a newline, while the result string is unbounded by
        // newlines. Also, we don't want this test to fail if the only change is to json
        // format within the reader.
        pretty_results.retain(|c| !c.is_whitespace());
        expected_string.retain(|c| !c.is_whitespace());

        assert_diff!(&expected_string, &pretty_results, "", 0);

        // Need to return to break loop when read succeeds.
        return;
    }
}

#[fasync::run_singlethreaded(test)]
async fn unified_reader() -> Result<(), Error> {
    // We need to keep example_app in scope so it stays running until the end
    // of the test.
    let (archivist_app, _example_app) = setup_environment().await?;

    // First, retrieve all of the information in our realm to make sure that everything
    // we expect is present.
    retrieve_and_validate_results(&archivist_app, Vec::new(), &*ALL_GOLDEN_JSON_PATH, 4)
        .on_timeout(READ_TIMEOUT_SECONDS.seconds().after_now(), || {
            panic!("failed to get meaningful results from reader service.")
        })
        .await;

    // Then verify that from the expected data, we can retrieve one specific value.
    retrieve_and_validate_results(
        &archivist_app,
        vec![&mut SelectorArgument::RawSelector(
            "iquery_example_component.cmx:*:lazy-*".to_string(),
        )],
        &*SINGLE_VALUE_CLIENT_SELECTOR_JSON_PATH,
        1,
    )
    .on_timeout(READ_TIMEOUT_SECONDS.seconds().after_now(), || {
        panic!("failed to get meaningful results from reader service.")
    })
    .await;
    Ok(())
}
