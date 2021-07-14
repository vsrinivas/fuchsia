// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// [START main]
use anyhow::{self, Error};
use tracing;

/// Creates a simple session that just prints "Hello World" to the syslog.
#[fuchsia::component(logging = true)]
async fn main() -> Result<(), Error> {
    tracing::info!("Hello World!");

    Ok(())
}
// [END main]

#[cfg(test)]
mod tests {
    #[fuchsia::test]
    async fn smoke_test() {
        assert!(true);
    }
}
