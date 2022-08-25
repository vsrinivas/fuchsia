// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#[cfg(all(target_os = "fuchsia", feature = "b"))]
compile_error!("the 'b' feature is not supported on Fuchsia");
