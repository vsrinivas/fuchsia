// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::ie::SupportedRate,
    failure::{format_err, Error},
    std::collections::HashSet,
};

pub struct ApRates<'a>(pub &'a [SupportedRate]);
pub struct ClientRates<'a>(pub &'a [SupportedRate]);

/// Returns the rates specified by the AP that are also supported by the client, with basic bits
/// following their values in the AP.
/// Returns Error if intersection fails.
/// Note: The client MUST support ALL the basic rates specified by the AP or the intersection fails.
pub fn intersect_rates(
    ap_rates: ApRates,
    client_rates: ClientRates,
) -> Result<Vec<SupportedRate>, Error> {
    let mut rates = ap_rates.0.to_vec();
    let client_rates = client_rates.0.iter().map(|r| r.rate()).collect::<HashSet<_>>();
    // The client MUST support ALL basic rates specified by the AP.
    if rates.iter().any(|ra| ra.basic() && !client_rates.contains(&ra.rate())) {
        return Err(format_err!("At least one basic rate not supported."));
    }

    // Remove rates that are not supported by the client.
    rates.retain(|ra| client_rates.contains(&ra.rate()));
    if rates.is_empty() {
        Err(format_err!("Client does not support any AP rates."))
    } else {
        Ok(rates)
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    impl SupportedRate {
        fn new_basic(rate: u8) -> Self {
            Self(rate).with_basic(true)
        }
    }

    #[test]
    fn some_basic_rate_missing() {
        // AP basic rate 120 is not supported, resulting in an Error
        let error = intersect_rates(
            ApRates(&[SupportedRate::new_basic(120), SupportedRate::new_basic(111)][..]),
            ClientRates(&[SupportedRate(111)][..]),
        )
        .unwrap_err();
        assert!(format!("{}", error).contains("At least one basic rate not supported."));
    }

    #[test]
    fn all_basic_rates_supported() {
        assert_eq!(
            vec![SupportedRate::new_basic(120)],
            intersect_rates(
                ApRates(&[SupportedRate::new_basic(120), SupportedRate(111)][..]),
                ClientRates(&[SupportedRate(120)][..])
            )
            .unwrap()
        );
    }

    #[test]
    fn all_basic_and_non_basic_rates_supported() {
        assert_eq!(
            vec![SupportedRate::new_basic(120)],
            intersect_rates(
                ApRates(&[SupportedRate::new_basic(120), SupportedRate(111)][..]),
                ClientRates(&[SupportedRate(120)][..])
            )
            .unwrap()
        );
    }

    #[test]
    fn no_rates_are_supported() {
        let error =
            intersect_rates(ApRates(&[SupportedRate(120)][..]), ClientRates(&[][..])).unwrap_err();
        assert!(format!("{}", error).contains("Client does not support any AP rates."));
    }

    #[test]
    fn preserve_ap_rates_basicness() {
        // AP side 120 is not basic so the result should be non-basic.
        assert_eq!(
            vec![SupportedRate(120)],
            intersect_rates(
                ApRates(&[SupportedRate(120), SupportedRate(111)][..]),
                ClientRates(&[SupportedRate::new_basic(120)][..])
            )
            .unwrap()
        );
    }
}
