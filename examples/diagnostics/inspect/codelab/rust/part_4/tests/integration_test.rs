// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Error,
    inspect_codelab_testing::{IntegrationTest, TestOptions},
};

// [START integration_test]
#[fuchsia::test]
async fn start_with_fizzbuzz() -> Result<(), Error> {
    let test = IntegrationTest::start(4, TestOptions::default()).await?;
    let reverser = test.connect_to_reverser()?;
    let result = reverser.reverse("hello").await?;
    assert_eq!(result, "olleh");

    // CODELAB: Check that the component was connected to FizzBuzz.

    Ok(())
}
// [END integration_test]

#[fuchsia::test]
async fn start_without_fizzbuzz() -> Result<(), Error> {
    let test = IntegrationTest::start(4, TestOptions { include_fizzbuzz: false }).await?;
    let reverser = test.connect_to_reverser()?;
    let result = reverser.reverse("hello").await?;
    assert_eq!(result, "olleh");

    // CODELAB: Check that the component failed to connect to FizzBuzz.

    Ok(())
}
