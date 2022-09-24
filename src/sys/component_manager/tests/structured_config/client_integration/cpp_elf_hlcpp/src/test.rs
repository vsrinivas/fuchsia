// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#[fuchsia::test]
async fn client_integration_test() {
    sc_client_integration_support::run_test_case("cpp_elf_hlcpp_receiver:root").await;
}
