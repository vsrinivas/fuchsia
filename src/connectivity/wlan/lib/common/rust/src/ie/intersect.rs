// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::ie::SupportedRate,
    std::collections::HashSet,
    zerocopy::LayoutVerified,
};

pub struct ApRates<'a>(pub &'a [SupportedRate]);
pub struct ClientRates<'a>(pub &'a [SupportedRate]);

impl <'a> From<&'a [u8]> for ApRates<'a> {
    fn from(rates: &'a [u8]) -> Self {
        // This is always safe, as SupportedRate is a newtype of u8.
        Self(LayoutVerified::new_slice(rates).unwrap().into_slice())
    }
}

impl <'a> From<&'a [u8]> for ClientRates<'a> {
    fn from(rates: &'a [u8]) -> Self {
        // This is always safe, as SupportedRate is a newtype of u8.
        Self(LayoutVerified::new_slice(rates).unwrap().into_slice())
    }
}

#[derive(Eq, PartialEq, Debug)]
pub enum IntersectRatesError {
    BasicRatesMismatch,
    NoApRatesSupported,
}

/// Returns the rates specified by the AP that are also supported by the client, with basic bits
/// following their values in the AP.
/// Returns Error if intersection fails.
/// Note: The client MUST support ALL the basic rates specified by the AP or the intersection fails.
pub fn intersect_rates(
    ap_rates: ApRates,
    client_rates: ClientRates,
) -> Result<Vec<SupportedRate>, IntersectRatesError> {
    let mut rates = ap_rates.0.to_vec();
    let client_rates = client_rates.0.iter().map(|r| r.rate()).collect::<HashSet<_>>();
    // The client MUST support ALL basic rates specified by the AP.
    if rates.iter().any(|ra| ra.basic() && !client_rates.contains(&ra.rate())) {
        return Err(IntersectRatesError::BasicRatesMismatch);
    }

    // Remove rates that are not supported by the client.
    rates.retain(|ra| client_rates.contains(&ra.rate()));
    if rates.is_empty() {
        Err(IntersectRatesError::NoApRatesSupported)
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
        assert_eq!(error, IntersectRatesError::BasicRatesMismatch);
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
        assert_eq!(error, IntersectRatesError::NoApRatesSupported);
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
