// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Error,
    fidl_fuchsia_examples_inspect::ReverserProxy,
    fuchsia_async as fasync,
    fuchsia_component::server::ServiceFs,
    futures::StreamExt,
    inspect_codelab_shared::CodelabEnvironment,
    lazy_static::lazy_static,
    std::sync::atomic::{AtomicUsize, Ordering},
};

// [START include_test_stuff]
use {
    anyhow::format_err,
    fuchsia_inspect::testing::{assert_inspect_tree, AnyProperty},
    fuchsia_inspect_contrib::reader::{ArchiveReader, ComponentSelector, NodeHierarchy},
};
// [END include_test_stuff]

lazy_static! {
    static ref SUFFIX: AtomicUsize = AtomicUsize::new(0);
}

struct TestOptions {
    include_fizzbuzz: bool,
}

impl Default for TestOptions {
    fn default() -> Self {
        TestOptions { include_fizzbuzz: true }
    }
}

struct IntegrationTest {
    env: CodelabEnvironment,
    environment_label: String,
}

impl IntegrationTest {
    fn start() -> Result<Self, Error> {
        let mut fs = ServiceFs::new();
        let suffix = SUFFIX.fetch_add(1, Ordering::SeqCst);
        let environment_label = format!("{}_{}", "test", suffix);
        let env = CodelabEnvironment::new(
            fs.create_nested_environment(&environment_label)?,
            "inspect_rust_codelab_integration_tests",
            5,
        );
        fasync::Task::spawn(fs.collect::<()>()).detach();
        Ok(Self { env, environment_label })
    }

    fn start_component_and_connect(
        &mut self,
        options: TestOptions,
    ) -> Result<ReverserProxy, Error> {
        if options.include_fizzbuzz {
            self.env.launch_fizzbuzz()?;
        }
        self.env.launch_reverser()
    }

    // [START get_inspect]
    async fn get_inspect_hierarchy(&self) -> Result<NodeHierarchy, Error> {
        ArchiveReader::new()
            .add_selector(ComponentSelector::new(vec![
                self.environment_label.clone(),
                "inspect_rust_codelab_part_5.cmx".to_string(),
            ]))
            .get()
            .await?
            .into_iter()
            .next()
            .and_then(|result| result.payload)
            .ok_or(format_err!("expected one inspect hierarchy"))
    }
    // [END get_inspect]
}

#[fasync::run_singlethreaded(test)]
async fn start_with_fizzbuzz() -> Result<(), Error> {
    let mut test = IntegrationTest::start()?;
    let reverser = test.start_component_and_connect(TestOptions::default())?;
    let result = reverser.reverse("hello").await?;
    assert_eq!(result, "olleh");

    // [START result_hierarchy]
    let hierarchy = test.get_inspect_hierarchy().await?;
    // [END result_hierarchy]
    assert_inspect_tree!(hierarchy, root: contains {
        "fuchsia.inspect.Health": contains {
            status: "OK",
            // The metric with a timestamp has an unpredictable value, so
            // we only assert that it is present.
            start_timestamp_nanos: AnyProperty,
        }
    });

    Ok(())
}

#[fasync::run_singlethreaded(test)]
async fn start_without_fizzbuzz() -> Result<(), Error> {
    let mut test = IntegrationTest::start()?;
    let reverser = test.start_component_and_connect(TestOptions { include_fizzbuzz: false })?;
    let result = reverser.reverse("hello").await?;
    assert_eq!(result, "olleh");

    let hierarchy = test.get_inspect_hierarchy().await?;
    assert_inspect_tree!(hierarchy, root: contains {
        "fuchsia.inspect.Health": contains {
            status: "UNHEALTHY",
            message: "FizzBuzz connection closed",
            // Not inspecting the metric `start_timestamp_nanos` which is also
            // present here.
        }
    });
    Ok(())
}
