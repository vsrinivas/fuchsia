// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use hub_report::*;

#[fuchsia::main]
async fn main() {
    expect_dir_listing("/hub/children", vec!["child"]).await;
    expect_dir_listing("/hub/children/child", vec!["children"]).await;
    expect_dir_listing("/hub/children/child/children", vec![]).await;
}
