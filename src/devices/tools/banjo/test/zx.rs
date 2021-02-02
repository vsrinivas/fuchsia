// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Note: this needs to be a macro as it will be used in a macro.

#[macro_export]
macro_rules! zx {
    () => {
        "../../../../../sdk/banjo/zx/zx.banjo"
    };
}
