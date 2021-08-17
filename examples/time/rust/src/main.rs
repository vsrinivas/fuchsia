// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#[fuchsia::component]
async fn main() {
    monotonic::monotonic_examples();
    utc::utc_examples().await;
}

mod monotonic {
    // [START monotonic]
    use fuchsia_zircon as zx;

    pub fn monotonic_examples() {
        // Read monotonic time.
        let monotonic_time = zx::Time::get_monotonic();
        println!("The monotonic time is {:?}.", monotonic_time);
    }
    // [END monotonic]
}

mod utc {
    // [START utc]
    use fuchsia_async as fasync;
    use fuchsia_runtime::duplicate_utc_clock_handle;
    use fuchsia_zircon as zx;

    pub async fn utc_examples() {
        // Obtain a UTC handle.
        let utc_clock = duplicate_utc_clock_handle(zx::Rights::SAME_RIGHTS)
            .expect("Failed to duplicate UTC clock handle.");

        // Wait for the UTC clock to start.
        fasync::OnSignals::new(&utc_clock, zx::Signals::CLOCK_STARTED)
            .await
            .expect("Failed to wait for ZX_CLOCK_STARTED.");
        println!("UTC clock is started.");

        // Read the UTC clock.
        let utc_time = utc_clock.read().expect("Failed to read UTC clock.");
        println!("The UTC time is {:?}.", utc_time);

        // Read UTC clock details.
        let clock_details = utc_clock.get_details().expect("Failed to read UTC clock details.");
        println!("The UTC clock's backstop time is {:?}.", clock_details.backstop);
    }
    // [END utc]
}
