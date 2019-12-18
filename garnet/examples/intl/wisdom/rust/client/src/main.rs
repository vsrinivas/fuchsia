// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Intl wisdom client
//!
//! This is an example implementation of the international wisdom client program.
//! It is not a "real" client library since it starts both the server *and* the
//! client side of the connection and attaches them together in a demo.  But it
//! does show how one can use the available ICU bindings for rust to build a
//! program that makes use of the Unicode support within ICU.

use {
    failure::{Error, ResultExt},
    fidl_fuchsia_examples_intl_wisdom as fwisdom, fuchsia_async as fasync,
    fuchsia_component::client::{launch, launcher},
    icu_data, rust_icu_sys as usys, rust_icu_udat as udat, rust_icu_uloc as uloc,
    rust_icu_ustring as ustring,
    std::convert::TryFrom,
    structopt::StructOpt,
};

pub(crate) mod wisdom_client_impl;

#[derive(StructOpt, Debug)]
#[structopt(name = "intl_wisdom_client_rust")]
struct Opt {
    #[structopt(
        long = "server",
        help = "URL of the intl wisdom server",
        default_value = "fuchsia-pkg://fuchsia.com/intl_wisdom_rust#meta/intl_wisdom_server_rust.cmx"
    )]
    server_url: String,

    #[structopt(
        long = "timestamp",
        help = "the date-time to request the timestamp for",
        default_value = "2018-10-30T15:30:00-07:00"
    )]
    timestamp: String,

    #[structopt(
        long = "timezone",
        help = "the time zone to request the printout for",
        default_value = "Etc/Unknown"
    )]
    timezone: String,
}

// Parses a textual timestamp like "2018-10-30T15:30:00-07:00" into a date-time point.
fn parse_timestamp(timestamp: &str, timezone: &str) -> Result<usys::UDate, Error> {
    let pattern = ustring::UChar::try_from("yyyy-MM-dd'T'HH:mm:ssXX")?;
    let loc = uloc::ULoc::try_from("en-US")?;

    // COMPATIBILITY: This doesn't use the system time zone when converting the timestamp, but
    // rather the passed-in timezone.
    let tz_id = ustring::UChar::try_from(timezone)?;
    let fmt = udat::UDateFormat::new_with_pattern(&loc, &tz_id, &pattern)?;
    fmt.parse(timestamp).map_err(|e| e.into())
}

#[fasync::run_singlethreaded]
async fn main() -> Result<(), Error> {
    // Force the loading of ICU data at the beginning of the program.
    let icu_data_loader = icu_data::Loader::new()?;

    // Launch the server and connect to the echo service.
    let opts: Opt = Opt::from_args();

    let launcher = launcher().context("Failed to open launcher service")?;
    let app =
        launch(&launcher, opts.server_url, None).context("Failed to launch intl wisdom service")?;

    let wisdom = app
        .connect_to_service::<fwisdom::IntlWisdomServer_Marker>()
        .context("failed to connect to intl wisdom service")?;

    let timestamp_ms = parse_timestamp(&opts.timestamp, &opts.timezone)?;

    let client = wisdom_client_impl::Client::new(icu_data_loader.clone(), wisdom);
    let res = client.ask_for_wisdom(timestamp_ms, &opts.timezone).await?;
    println!("Response:\n{}", res);
    Ok(())
}

#[cfg(test)]
mod tests {
    #[test]
    fn basic() {
        assert!(true);
    }
}
