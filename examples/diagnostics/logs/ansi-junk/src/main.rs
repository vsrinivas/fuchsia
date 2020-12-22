// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#[fuchsia::component]
async fn main() -> Result<(), anyhow::Error> {
    tracing::info!("Initialized.");
    tracing::info!("Bell: \x1b[\x07");
    tracing::info!("\x1b[32;1mstart green");
    Ok(())
}

#[cfg(test)]
mod tests {
    #[fuchsia::test]
    async fn smoke_test() {
        assert!(true);
    }
}
