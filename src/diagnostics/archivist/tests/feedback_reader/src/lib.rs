// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.
use {
    anyhow::{bail, Error},
    fidl_fuchsia_diagnostics::{ArchiveAccessorMarker, DataType},
    fidl_fuchsia_sys::ComponentControllerEvent,
    fuchsia_async::{self as fasync, DurationExt, TimeoutExt},
    fuchsia_component::client::{launch, launcher},
    fuchsia_component::client::{App, AppBuilder},
    fuchsia_inspect_contrib::reader::ArchiveReader,
    fuchsia_zircon::DurationNum,
    futures::StreamExt,
    lazy_static::lazy_static,
    pretty_assertions::assert_eq,
    std::{
        fs::{self, create_dir, create_dir_all, remove_dir_all, write, File},
        path::{Path, PathBuf},
    },
};

const ARCHIVIST_URL: &str =
    "fuchsia-pkg://fuchsia.com/archivist-integration-tests#meta/feedback_reader_test_archivist.cmx";
const SERVICE_URL: &str =
    "fuchsia-pkg://fuchsia.com/archivist-integration-tests#meta/iquery_test_component.cmx";

const ARCHIVIST_CONFIG: &[u8] = include_bytes!("../configs/archivist_config.json");
const ALL_GOLDEN_JSON: &[u8] = include_bytes!("../configs/all_golden.json");
const SINGLE_VALUE_CLIENT_SELECTOR_JSON: &[u8] =
    include_bytes!("../configs/single_value_client_selector.json");
const NONOVERLAPPING_CLIENT_AND_STATIC_SELECTORS_JSON: &[u8] =
    include_bytes!("../configs/client_selectors_dont_overlap_with_static_selectors.json");

const STATIC_FEEDBACK_SELECTORS: &[u8] = include_bytes!("../configs/static_selectors.cfg");
// Number of seconds to wait before timing out polling the reader for pumped results.

static READ_TIMEOUT_SECONDS: i64 = 60;

static MONIKER_KEY: &str = "moniker";
static METADATA_KEY: &str = "metadata";
static TIMESTAMP_KEY: &str = "timestamp";
static TEST_ARCHIVIST: &str = "feedback_reader_test_archivist.cmx";

lazy_static! {
    static ref CONFIG_PATH: PathBuf = Path::new("/tmp/config/data").to_path_buf();
    static ref ARCHIVIST_CONFIGURATION_PATH: PathBuf = CONFIG_PATH.join("archivist_config.json");
    static ref STATIC_SELECTORS_PATH: PathBuf = CONFIG_PATH.join("feedback/static_selectors.cfg");
    static ref DISABLE_FILTER_PATH: PathBuf = CONFIG_PATH.join("feedback/DISABLE_FILTERING.txt");
    static ref ALL_GOLDEN_JSON_PATH: PathBuf = CONFIG_PATH.join("all_golden.json");
    static ref EMPTY_RESULTS_GOLDEN_JSON_PATH: PathBuf =
        CONFIG_PATH.join("client_selectors_dont_overlap_with_static_selectors.json");
    static ref SINGLE_VALUE_CLIENT_SELECTOR_JSON_PATH: PathBuf =
        CONFIG_PATH.join("single_value_client_selector.json");
    static ref ARCHIVE_PATH: &'static str = "/tmp/archive";
    static ref TEST_DATA_PATH: &'static str = "/tmp/test_data";
}

struct TestOptions {
    // If true, inject the special file to disable filtering.
    disable_filtering: bool,
    // If true, omit selectors from the test so the pipeline is unconfigured.
    omit_selectors: bool,
}

async fn setup_environment(test_options: TestOptions) -> Result<(App, App), Error> {
    remove_dir_all(&*CONFIG_PATH).unwrap_or_default();
    remove_dir_all(&*ARCHIVE_PATH).unwrap_or_default();
    remove_dir_all(&*TEST_DATA_PATH).unwrap_or_default();
    create_dir_all(&*STATIC_SELECTORS_PATH.parent().unwrap())?;
    create_dir(*ARCHIVE_PATH)?;
    create_dir(*TEST_DATA_PATH)?;

    write(&*ARCHIVIST_CONFIGURATION_PATH, ARCHIVIST_CONFIG)?;
    if !test_options.omit_selectors {
        write(&*STATIC_SELECTORS_PATH, STATIC_FEEDBACK_SELECTORS)?;
    }
    if test_options.disable_filtering {
        write(&*DISABLE_FILTER_PATH, "This file disables filtering")?;
    }
    write(&*ALL_GOLDEN_JSON_PATH, ALL_GOLDEN_JSON)?;
    write(&*SINGLE_VALUE_CLIENT_SELECTOR_JSON_PATH, SINGLE_VALUE_CLIENT_SELECTOR_JSON)?;
    write(&*EMPTY_RESULTS_GOLDEN_JSON_PATH, NONOVERLAPPING_CLIENT_AND_STATIC_SELECTORS_JSON)?;

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
        .expect("component event stream has ended before termination event")?
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

fn process_results_for_comparison(results: serde_json::Value) -> serde_json::Value {
    let mut string_result_array = results
        .as_array()
        .expect("result json is an array of objs.")
        .into_iter()
        .filter_map(|val| {
            let mut val = val.clone();
            // Filter out the results coming from the archivist, and zero out timestamps
            // that we cant golden test.
            val.as_object_mut().map_or(
                None,
                |obj: &mut serde_json::Map<String, serde_json::Value>| match obj.get(MONIKER_KEY) {
                    Some(serde_json::Value::String(moniker_str)) => {
                        if moniker_str != TEST_ARCHIVIST {
                            let metadata_obj =
                                obj.get_mut(METADATA_KEY).unwrap().as_object_mut().unwrap();
                            metadata_obj.insert(TIMESTAMP_KEY.to_string(), serde_json::json!(0));
                            Some(
                                serde_json::to_string(&serde_json::to_value(obj).unwrap())
                                    .expect("All entries in the array are valid."),
                            )
                        } else {
                            None
                        }
                    }
                    _ => None,
                },
            )
        })
        .collect::<Vec<String>>();

    string_result_array.sort();
    let sorted_results_json_string = format!("[{}]", string_result_array.join(","));
    eprintln!("processed to {} bytes", sorted_results_json_string.len());
    serde_json::from_str(&sorted_results_json_string).unwrap()
}

async fn feedback_pipeline_is_filtered(archivist: &App, expected_results_count: usize) -> bool {
    let feedback_archive_accessor = archivist
        .connect_to_named_service::<ArchiveAccessorMarker>(
            "fuchsia.diagnostics.FeedbackArchiveAccessor",
        )
        .unwrap();

    let feedback_results = ArchiveReader::new()
        .with_archive(feedback_archive_accessor)
        .with_minimum_schema_count(expected_results_count)
        .snapshot_raw(DataType::Inspect)
        .await
        .expect("got result");

    let all_archive_accessor = archivist
        .connect_to_named_service::<ArchiveAccessorMarker>("fuchsia.diagnostics.ArchiveAccessor")
        .unwrap();

    let all_results = ArchiveReader::new()
        .with_archive(all_archive_accessor)
        .with_minimum_schema_count(expected_results_count)
        .snapshot_raw(DataType::Inspect)
        .await
        .expect("got result");

    process_results_for_comparison(feedback_results) != process_results_for_comparison(all_results)
}

// Loop indefinitely snapshotting the archive until we get the expected number of
// hierarchies, and then validate that the ordered json represetionation of these hierarchies
// matches the golden file.
async fn retrieve_and_validate_results(
    archivist: &App,
    custom_selectors: Vec<&str>,
    golden_file: &PathBuf,
    expected_results_count: usize,
) {
    let archive_accessor = archivist
        .connect_to_named_service::<ArchiveAccessorMarker>(
            "fuchsia.diagnostics.FeedbackArchiveAccessor",
        )
        .unwrap();

    let results = ArchiveReader::new()
        .with_archive(archive_accessor)
        .add_selectors(custom_selectors.clone().into_iter())
        .with_minimum_schema_count(expected_results_count)
        .snapshot_raw(DataType::Inspect)
        .await
        .expect("got result");

    // Convert the json struct into a "pretty" string rather than converting the
    // golden file into a json struct because deserializing the golden file into a
    // struct causes serde_json to convert the u64s into exponential form which
    // causes loss of precision.
    let mut pretty_results = serde_json::to_string_pretty(&process_results_for_comparison(results))
        .expect("should be able to format the the results as valid json.");
    let mut expected_string: String =
        fs::read_to_string(golden_file).expect("Reading golden json failed.");
    // Remove whitespace from both strings because text editors will do things like
    // requiring json files end in a newline, while the result string is unbounded by
    // newlines. Also, we don't want this test to fail if the only change is to json
    // format within the reader.
    pretty_results.retain(|c| !c.is_whitespace());
    expected_string.retain(|c| !c.is_whitespace());
    assert_eq!(&expected_string, &pretty_results, "goldens mismatch");
}

#[fasync::run_singlethreaded(test)]
async fn canonical_reader_test() -> Result<(), Error> {
    // We need to keep example_app in scope so it stays running until the end
    // of the test.
    let (archivist_app, _example_app) =
        setup_environment(TestOptions { disable_filtering: false, omit_selectors: false }).await?;
    // First, retrieve all of the information in our realm to make sure that everything
    // we expect is present.
    retrieve_and_validate_results(&archivist_app, Vec::new(), &*ALL_GOLDEN_JSON_PATH, 3)
        .on_timeout(READ_TIMEOUT_SECONDS.seconds().after_now(), || {
            panic!("failed to get meaningful results from reader service.")
        })
        .await;
    // Then verify that from the expected data, we can retrieve one specific value.
    retrieve_and_validate_results(
        &archivist_app,
        vec!["iquery_test_component.cmx:*:lazy-*"],
        &*SINGLE_VALUE_CLIENT_SELECTOR_JSON_PATH,
        3,
    )
    .on_timeout(READ_TIMEOUT_SECONDS.seconds().after_now(), || {
        panic!("failed to get meaningful results from reader service.")
    })
    .await;
    // Then verify that subtree selection retrieves all trees under and including root.
    retrieve_and_validate_results(
        &archivist_app,
        vec!["iquery_test_component.cmx:root"],
        &*ALL_GOLDEN_JSON_PATH,
        3,
    )
    .on_timeout(READ_TIMEOUT_SECONDS.seconds().after_now(), || {
        panic!("failed to get meaningful results from reader service.")
    })
    .await;
    // Then verify that client selectors dont override the static selectors provided
    // to the archivist.
    retrieve_and_validate_results(
        &archivist_app,
        vec![r#"iquery_test_component.cmx:root:array\:0x15"#],
        &*EMPTY_RESULTS_GOLDEN_JSON_PATH,
        3,
    )
    .on_timeout(READ_TIMEOUT_SECONDS.seconds().after_now(), || {
        panic!("failed to get meaningful results from reader service.")
    })
    .await;

    assert!(feedback_pipeline_is_filtered(&archivist_app, 3).await);

    Ok(())
}

#[fasync::run_singlethreaded(test)]
async fn test_disabled_pipeline() -> Result<(), Error> {
    let (archivist_app, _example_app) =
        setup_environment(TestOptions { disable_filtering: true, omit_selectors: false }).await?;
    assert!(!feedback_pipeline_is_filtered(&archivist_app, 3).await);

    Ok(())
}

#[fasync::run_singlethreaded(test)]
async fn test_pipeline_missing_selectors() -> Result<(), Error> {
    let (archivist_app, _example_app) =
        setup_environment(TestOptions { disable_filtering: false, omit_selectors: true }).await?;
    assert!(!feedback_pipeline_is_filtered(&archivist_app, 3).await);

    Ok(())
}
