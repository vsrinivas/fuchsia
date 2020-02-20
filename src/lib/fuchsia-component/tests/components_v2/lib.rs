// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {anyhow::Error, fuchsia_async as fasync, test_utils_lib::test_utils};

// TODO: Once (blocking) events are available, this test can become a pure CFv2 test.

#[fasync::run_singlethreaded(test)]
async fn test() -> Result<(), Error> {
    test_utils::launch_component_and_expect_output(
        "fuchsia-pkg://fuchsia.com/fuchsia-component-tests#meta/realm.cm",
        "Done\n".to_string(),
    ).await
}
