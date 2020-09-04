// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use cml::error::Error;

mod compile;

fn main() -> Result<(), Error> {
    compile::from_args()
}
