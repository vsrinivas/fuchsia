// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {anyhow::Result, scrutiny_frontend};

fn main() -> Result<()> {
    scrutiny_frontend::launcher::launch()
}
