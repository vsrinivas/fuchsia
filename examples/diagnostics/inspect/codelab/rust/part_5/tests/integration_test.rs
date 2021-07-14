// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Error,
    inspect_codelab_testing::{IntegrationTest, TestOptions},
};

// [START include_test_stuff]
use {
    anyhow::format_err,
    diagnostics_reader::{
        assert_data_tree, AnyProperty, ArchiveReader, DiagnosticsHierarchy, Inspect,
    },
};
// [END include_test_stuff]

// [START get_inspect]
async fn get_inspect_hierarchy(test: &IntegrationTest) -> Result<DiagnosticsHierarchy, Error> {
    let moniker = test.reverser_moniker_for_selectors();
    ArchiveReader::new()
        .add_selector(format!("{}:root", moniker))
        .snapshot::<Inspect>()
        .await?
        .into_iter()
        .next()
        .and_then(|result| result.payload)
        .ok_or(format_err!("expected one inspect hierarchy"))
}
// [END get_inspect]

#[fuchsia::test]
async fn start_with_fizzbuzz() -> Result<(), Error> {
    let test = IntegrationTest::start(5, TestOptions::default()).await?;
    let reverser = test.connect_to_reverser()?;
    let result = reverser.reverse("hello").await?;
    assert_eq!(result, "olleh");

    // [START result_hierarchy]
    let hierarchy = get_inspect_hierarchy(&test).await?;
    // [END result_hierarchy]
    assert_data_tree!(hierarchy, root: contains {
        "fuchsia.inspect.Health": contains {
            status: "OK",
            // The metric with a timestamp has an unpredictable value, so
            // we only assert that it is present.
            start_timestamp_nanos: AnyProperty,
        }
    });

    Ok(())
}

#[fuchsia::test]
async fn start_without_fizzbuzz() -> Result<(), Error> {
    let test = IntegrationTest::start(5, TestOptions { include_fizzbuzz: false }).await?;
    let reverser = test.connect_to_reverser()?;
    let result = reverser.reverse("hello").await?;
    assert_eq!(result, "olleh");

    let hierarchy = get_inspect_hierarchy(&test).await?;
    assert_data_tree!(hierarchy, root: contains {
        "fuchsia.inspect.Health": contains {
            status: "UNHEALTHY",
            message: "FizzBuzz connection closed",
            // Not inspecting the metric `start_timestamp_nanos` which is also
            // present here.
        }
    });
    Ok(())
}
