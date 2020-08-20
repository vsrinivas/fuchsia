// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! See the `README.md` file in this directory for more detail.

#[cfg(test)]
mod tests {

    use {
        anyhow::{Context as _, Error},
        crossbeam::channel,
        fidl_fidl_examples_echo as fecho, fidl_fuchsia_intl as fintl,
        fidl_fuchsia_settings as fsettings, fuchsia_async as fasync,
        fuchsia_component::client,
        fuchsia_syslog::{self as syslog, macros::*},
        icu_data, rust_icu_ucal as ucal, rust_icu_udat as udat, rust_icu_uloc as uloc,
        rust_icu_ustring as ustring,
        std::convert::TryFrom,
    };

    /// Sets a timezone for the duration of its lifetime, and restores the previous timezone at the
    /// end.
    pub struct ScopedTimezone {
        timezone: String,
        client: fsettings::IntlProxy,
    }

    fn intl_client() -> Result<fsettings::IntlProxy, Error> {
        client::connect_to_service::<fsettings::IntlMarker>()
            .context("Failed to connect to fuchsia.settings.Intl")
    }

    impl ScopedTimezone {
        /// Tries to create a new timezone setter.
        pub async fn try_new(timezone: &str) -> Result<ScopedTimezone, Error> {
            let client = intl_client()?;
            let response = client.watch().await?;
            fx_log_info!("setting timezone for test: {}", timezone);
            let old_timezone = response.time_zone_id.unwrap().id;
            let setter = ScopedTimezone { timezone: old_timezone, client: client };
            setter.set_timezone(timezone).await?;
            Ok(setter)
        }

        /// Asynchronously set the timezone based to the supplied value.
        async fn set_timezone(&self, timezone: &str) -> Result<(), Error> {
            fx_log_info!("setting timezone: {}", timezone);
            let settings = fsettings::IntlSettings {
                time_zone_id: Some(fintl::TimeZoneId { id: timezone.to_string() }),
                locales: None,
                temperature_unit: None,
                hour_cycle: None,
            };
            let _result = self.client.set(settings).await.context("setting timezone").unwrap();
            Ok(())
        }
    }

    impl Drop for ScopedTimezone {
        /// Restores the previous timezone setting on the system.
        fn drop(&mut self) {
            let timezone = self.timezone.clone();
            let (tx, rx) = channel::bounded(0);
            // This piece of gymnastics is needed because the FIDL call we make
            // here is async, but `drop` implementation must be sync.
            std::thread::spawn(move || {
                let mut executor = fasync::Executor::new().unwrap();
                executor.run_singlethreaded(async move {
                    fx_log_info!("restoring timezone: {}", &timezone);
                    let client = intl_client().unwrap();
                    let settings = fsettings::IntlSettings {
                        time_zone_id: Some(fintl::TimeZoneId { id: timezone }),
                        locales: None,
                        temperature_unit: None,
                        hour_cycle: None,
                    };
                    let _result = client.set(settings).await.context("restoring timezone");
                    tx.send(()).unwrap();
                });
            });
            rx.recv().unwrap();
            fx_log_info!("restored timezone: {}", &self.timezone);
        }
    }

    // Implements the Echo service which serves an abbreviated form of current time.
    static DART_TIME_SERVICE_URL: &str =
        "fuchsia-pkg://fuchsia.com/timestamp-server-dart#meta/timestamp-server-dart.cmx";

    // The test will set this timezone to be the "system" timezone.
    // TODO(fxb/47043): Use another value for the time zone, e.g. "America/Los_Angeles".
    static TIMEZONE_NAME: &str = "America/Los_Angeles";

    /// Starts a dart program that uses Dart's idea of the system time zone to report time zone
    /// information.  The test fixture compares its own idea of local time with the one in the dart
    /// VM.  Ostensibly, those two times should be the same up to the current date and current
    /// hour.
    #[fasync::run_singlethreaded(test)]
    async fn check_reported_time_in_dart_vm() -> Result<(), Error> {
        syslog::init_with_tags(&["check_reported_time_in_dart_vm"]).context("Can't init logger")?;
        let _icu_data_loader =
            icu_data::Loader::new().with_context(|| "could not load ICU data")?;
        let _setter = ScopedTimezone::try_new(TIMEZONE_NAME)
            .await
            .context("Failed to instantiate ScopedTimezone")?;

        let formatter = udat::UDateFormat::new_with_pattern(
            &uloc::ULoc::try_from("Etc/Unknown")?,
            &ustring::UChar::try_from(TIMEZONE_NAME).expect("create string from timezone"),
            // Example: 2020-2-26-14, hour 14 of February 26.
            &ustring::UChar::try_from("yyyy-M-d-H")?,
        )?;

        let launcher = client::launcher().context("Failed to get the launcher")?;
        let app = client::launch(&launcher, DART_TIME_SERVICE_URL.to_string(), None)
            .context("failed to launch the dart service under test")?;

        let echo = app
            .connect_to_service::<fecho::EchoMarker>()
            .context("Failed to connect to echo service")?;

        // Multiple attempts in case the test is run close to the turn of the hour.
        for attempt in 1u8..3 {
            fx_log_info!("Requesting some time, attempt: {}", attempt);
            let out = echo
                .echo_string(Some("Gimme some time!"))
                .await
                .with_context(|| "echo_string failed")?;
            let date_time = formatter.format(ucal::get_now())?;
            let vm_time = out.unwrap();
            if date_time == vm_time {
                break;
            }
            if (date_time != vm_time) && attempt == 3 {
                assert_eq!(date_time, vm_time,
                    "dart VM says the time local in timezone {:?} is {:?}, but the test fixture says it should be: {:?}",
        TIMEZONE_NAME, vm_time, date_time);
            }
        }
        Ok(())
    }
} // tests

// Makes fargo happy.
fn main() {}
