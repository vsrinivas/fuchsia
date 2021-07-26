// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Error,
    inspect_rust_codelab_testing::{IntegrationTest, TestOptions},
};

#[fuchsia::test]
async fn start_with_fizzbuzz() -> Result<(), Error> {
    let test = IntegrationTest::start(1, TestOptions::default()).await?;
    let _reverser = test.connect_to_reverser()?;
    // CODELAB: uncomment when it works
    // let result = reverser.reverse("hello").await?;
    // assert_eq!(result, "olleh");
    Ok(())
}

#[fuchsia::test]
async fn start_without_fizzbuzz() -> Result<(), Error> {
    let test = IntegrationTest::start(1, TestOptions { include_fizzbuzz: false }).await?;
    let _reverser = test.connect_to_reverser()?;
    // CODELAB: uncomment when it works
    // let result = reverser.reverse("hello").await?;
    // assert_eq!(result, "olleh");
    Ok(())
}
