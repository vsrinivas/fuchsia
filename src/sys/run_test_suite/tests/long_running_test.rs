// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#[fuchsia::test]
async fn long_running() {
    fuchsia_async::Timer::new(std::time::Duration::from_secs(10)).await;
}
