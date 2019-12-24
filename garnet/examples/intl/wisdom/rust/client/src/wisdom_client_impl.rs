// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Error,
    //fidl_fuchsia_examples_intl_wisdom as fwisdom,
    fidl_fuchsia_examples_intl_wisdom as fwisdom,
    fidl_fuchsia_intl as fintl,
    rust_icu_sys as usys,
    rust_icu_ucal as ucal,
    rust_icu_udat as udat,
    rust_icu_uloc as uloc,
    rust_icu_ustring as ustring,
    std::convert::TryFrom,
};

// [START loader_example]
/// A client implementation that connects to the Wisdom server.
pub struct Client {
    // The proxy for calling into the Wisdom service.
    wisdom: fwisdom::IntlWisdomServer_Proxy,
    // Client requires the ICU data to be loaded for TZ parsing, so we keep
    // a reference to the data alive, even though we don't "use" the code.
    #[allow(dead_code)]
    icu_data_loader: icu_data::Loader,
}
// [END loader_example]

impl Client {
    /// Creates a new Client.  `wisdom` is the async proxy used to call into
    /// the intl wisdom service. `icu_data_loader` is a handle to the ICU locale data,
    /// which are required for the client to parse textual timestamps.
    pub fn new(icu_data_loader: icu_data::Loader, wisdom: fwisdom::IntlWisdomServer_Proxy) -> Self {
        Client { icu_data_loader, wisdom }
    }

    /// Sends a single request to the intl wisdom server's `AskForWisdom` method.
    pub async fn ask_for_wisdom(
        &self,
        timestamp_ms: usys::UDate,
        timezone: &str,
    ) -> Result<String, Error> {
        let profile = make_intl_profile(timezone)?;
        print!("Asking for wisdom...\n");
        let res = self.wisdom.ask_for_wisdom(profile, timestamp_ms as i64).await?.unwrap();
        Ok(res)
    }
}

// Returns the "short" timezone key, such as "usnyc" from "America/New_York".
fn tz_key(timezone: &str) -> Result<String, Error> {
    let loc = uloc::ULoc::try_from("en-US")?;
    let tz_id = ustring::UChar::try_from(timezone)?;
    // This is the pattern for the short timezone key.
    let pattern = ustring::UChar::try_from("V")?;
    let fmt = udat::UDateFormat::new_with_pattern(&loc, &tz_id, &pattern)?;
    fmt.format(ucal::get_now()).map_err(|e| e.into())
}

// Constructs an intl profile query with a few sample locales and calendars.
fn make_intl_profile(timezone: &str) -> Result<fintl::Profile, Error> {
    let tz_key = tz_key(timezone)?;
    let profile = fintl::Profile {
        locales: Some(vec![
            fintl::LocaleId {
                id: format!("fr-FR-u-ca-hebrew-fw-tuesday-nu-traditio-tz-{}", tz_key),
            },
            fintl::LocaleId {
                id: format!("es-MX-u-ca-hebrew-fw-tuesday-nu-traditio-tz-{}", tz_key),
            },
            fintl::LocaleId {
                id: format!("ru-PT-u-ca-hebrew-fw-tuesday-nu-traditio-tz-{}", tz_key),
            },
            fintl::LocaleId {
                id: format!("ar-AU-u-ca-hebrew-fw-tuesday-nu-traditio-tz-{}", tz_key),
            },
        ]),
        calendars: Some(vec![
            fintl::CalendarId { id: "und-u-ca-hebrew".to_string() },
            fintl::CalendarId { id: "und-u-ca-gregorian".to_string() },
            fintl::CalendarId { id: "und-u-ca-islamic".to_string() },
        ]),
        time_zones: Some(vec![fintl::TimeZoneId { id: timezone.to_string() }]),
        temperature_unit: None,
    };
    Ok(profile)
}
