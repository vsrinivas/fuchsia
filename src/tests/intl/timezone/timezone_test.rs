// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! See the `README.md` file in this directory for more detail.

#[cfg(test)]
mod tests {

    use anyhow::Error;
    use fuchsia_async as fasync;
    use fuchsia_syslog;
    use tests_intl_timezone;

    // Implements the Echo service which serves an abbreviated form of current time.
    pub static DART_TIME_SERVICE_URL: &str =
        "fuchsia-pkg://fuchsia.com/timestamp-server-dart#meta/timestamp-server-dart.cmx";

    /// Starts a dart program that uses Dart's idea of the system time zone to report time zone
    /// information.  The test fixture compares its own idea of local time with the one in the dart
    /// VM.
    ///
    /// Ostensibly, those two times should be the same up to the current date and current hour.
    ///
    /// We do this twice, to ensure that runtime timezone changes are reflected in the time
    /// reported by the dart VM.
    #[fasync::run_singlethreaded(test)]
    #[cfg_attr(target_arch = "aarch64", ignore)] // See http://fxbug.dev/60635
    async fn check_reported_time_in_dart_vm() -> Result<(), Error> {
        fuchsia_syslog::init_with_tags(&[
            "e2e",
            "timezone",
            "dart",
            "check_reported_time_in_dart_vm",
        ])
        .unwrap();
        tests_intl_timezone::check_reported_time_with_update(DART_TIME_SERVICE_URL).await
    }
} // tests

// Makes fargo happy.
fn main() {}
