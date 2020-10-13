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
    _environment_label: String,
}

impl IntegrationTest {
    fn start() -> Result<Self, Error> {
        let mut fs = ServiceFs::new();
        let suffix = SUFFIX.fetch_add(1, Ordering::SeqCst);
        let environment_label = format!("{}_{}", "test", suffix);
        let env = CodelabEnvironment::new(
            fs.create_nested_environment(&environment_label)?,
            "inspect_rust_codelab_integration_tests",
            3,
        );
        fasync::Task::spawn(fs.collect::<()>()).detach();
        Ok(Self { env, _environment_label: environment_label })
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
}

#[fasync::run_singlethreaded(test)]
async fn start_with_fizzbuzz() -> Result<(), Error> {
    let mut test = IntegrationTest::start()?;
    let reverser = test.start_component_and_connect(TestOptions::default())?;
    let result = reverser.reverse("hello").await?;
    assert_eq!(result, "olleh");
    Ok(())
}

#[fasync::run_singlethreaded(test)]
async fn start_without_fizzbuzz() -> Result<(), Error> {
    let mut test = IntegrationTest::start()?;
    let reverser = test.start_component_and_connect(TestOptions { include_fizzbuzz: false })?;
    let result = reverser.reverse("hello").await?;
    assert_eq!(result, "olleh");
    Ok(())
}
