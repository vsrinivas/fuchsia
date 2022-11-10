// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{format_err, Context as _, Error},
    fidl::endpoints,
    fidl_fuchsia_component as fcomponent, fidl_fuchsia_component_decl as fdecl,
    fidl_fuchsia_io as fio, fidl_fuchsia_test_manager as ftest_manager,
    ftest_manager::{CaseStatus, SuiteStatus},
    fuchsia_component::client,
    futures::{prelude::*, stream},
    pretty_assertions::assert_eq,
    test_manager_test_lib::{
        collect_suite_events, default_run_option, GroupRunEventByTestCase, RunEvent, TestBuilder,
        TestRunEventPayload,
    },
};

async fn connect_test_manager() -> Result<ftest_manager::RunBuilderProxy, Error> {
    let realm = client::connect_to_protocol::<fcomponent::RealmMarker>()
        .context("could not connect to Realm service")?;

    let mut child_ref = fdecl::ChildRef { name: "test_manager".to_owned(), collection: None };
    let (dir, server_end) = endpoints::create_proxy::<fio::DirectoryMarker>()?;
    realm
        .open_exposed_dir(&mut child_ref, server_end)
        .await
        .context("open_exposed_dir fidl call failed for test manager")?
        .map_err(|e| format_err!("failed to create test manager: {:?}", e))?;

    client::connect_to_protocol_at_dir_root::<ftest_manager::RunBuilderMarker>(&dir)
        .context("failed to open test suite service")
}

async fn debug_data_stress_test(case_name: &str, vmo_count: usize, vmo_size: usize) {
    const TEST_URL: &str =
        "fuchsia-pkg://fuchsia.com/test_manager_stress_test#meta/debug_data_spam_test.cm";

    let builder = TestBuilder::new(
        connect_test_manager().await.expect("cannot connect to run builder proxy"),
    );
    let mut options = default_run_option();
    options.case_filters_to_run = Some(vec![case_name.into()]);
    let suite_instance =
        builder.add_suite(TEST_URL, options).await.expect("Cannot create suite instance");
    let (run_events_result, suite_events_result) =
        futures::future::join(builder.run(), collect_suite_events(suite_instance)).await;

    let suite_events = suite_events_result.unwrap().0;
    let expected_events = vec![
        RunEvent::suite_started(),
        RunEvent::case_found(case_name),
        RunEvent::case_started(case_name),
        RunEvent::case_stopped(case_name, CaseStatus::Passed),
        RunEvent::case_finished(case_name),
        RunEvent::suite_stopped(SuiteStatus::Passed),
    ];

    assert_eq!(
        suite_events.into_iter().group_by_test_case_unordered(),
        expected_events.into_iter().group_by_test_case_unordered(),
    );

    let test_run_events = stream::iter(run_events_result.unwrap());
    let num_vmos = test_run_events
        .then(|run_event| async move {
            let TestRunEventPayload::DebugData { proxy, .. } = &run_event.payload;
            let contents = fuchsia_fs::read_file(&proxy).await.expect("read file");
            contents.len() == vmo_size && contents.as_bytes().iter().all(|byte| *byte == b'a')
        })
        .filter(|matches_vmo| futures::future::ready(*matches_vmo))
        .count()
        .await;
    assert_eq!(num_vmos, vmo_count);
}

#[fuchsia::test]
async fn debug_data_stress_test_many_vmos() {
    const NUM_EXPECTED_VMOS: usize = 3250;
    const VMO_SIZE: usize = 4096;
    const CASE_NAME: &'static str = "many_small_vmos";
    debug_data_stress_test(CASE_NAME, NUM_EXPECTED_VMOS, VMO_SIZE).await;
}

#[fuchsia::test]
async fn debug_data_stress_test_few_large_vmos() {
    const NUM_EXPECTED_VMOS: usize = 2;
    const VMO_SIZE: usize = 1024 * 1024 * 400;
    const CASE_NAME: &'static str = "few_large_vmos";
    debug_data_stress_test(CASE_NAME, NUM_EXPECTED_VMOS, VMO_SIZE).await;
}
