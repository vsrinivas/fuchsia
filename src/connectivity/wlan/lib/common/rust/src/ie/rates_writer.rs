// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        appendable::Appendable,
        error::FrameWriteError,
        ie::{
            write_ext_supported_rates, write_supported_rates, IE_MAX_LEN, SUPPORTED_RATES_MAX_LEN,
        },
    },
    zerocopy::ByteSlice,
};

pub struct RatesWriter<S>(S);

impl<S: ByteSlice> RatesWriter<S> {
    pub fn try_new(rates: S) -> Result<RatesWriter<S>, FrameWriteError> {
        if rates.len() == 0 {
            Err(FrameWriteError::new_invalid_data("no rates to write"))
        } else if rates.len() > SUPPORTED_RATES_MAX_LEN + IE_MAX_LEN {
            Err(FrameWriteError::new_invalid_data("rates will not fit in elements"))
        } else {
            Ok(RatesWriter(rates))
        }
    }

    pub fn write_supported_rates<B: Appendable>(&self, buf: &mut B) {
        let num_rates = std::cmp::min(self.0.len(), SUPPORTED_RATES_MAX_LEN);
        // safe to unwrap because we truncated the slice
        write_supported_rates(&mut *buf, &self.0[..num_rates]).unwrap();
    }

    pub fn write_ext_supported_rates<B: Appendable>(&self, buf: &mut B) {
        if self.0.len() > SUPPORTED_RATES_MAX_LEN {
            // safe to unwrap because it is guaranteed to fit.
            write_ext_supported_rates(&mut *buf, &self.0[SUPPORTED_RATES_MAX_LEN..]).unwrap();
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn empty_rates_error() {
        let rates = [];
        assert!(RatesWriter::try_new(&rates[..]).is_err());
    }

    #[test]
    fn too_many_rates_error() {
        let rates = [0; 1 + SUPPORTED_RATES_MAX_LEN + IE_MAX_LEN];
        assert!(RatesWriter::try_new(&rates[..]).is_err());
    }

    #[test]
    fn max_num_of_rates_ok() {
        let rates = [42; SUPPORTED_RATES_MAX_LEN + IE_MAX_LEN];
        assert!(RatesWriter::try_new(&rates[..]).is_ok());
    }

    #[test]
    fn rates_fit_in_supp_rates() {
        let rates: Vec<u8> = (0..SUPPORTED_RATES_MAX_LEN as u8).collect();
        let rates_writer = RatesWriter::try_new(&rates[..]).expect("Should be valid RatesWriter");
        let mut buf = vec![];
        rates_writer.write_supported_rates(&mut buf);
        rates_writer.write_ext_supported_rates(&mut buf);
        assert_eq!(
            &buf[..],
            &[
                1, 8, // ID and length
                0, 1, 2, 3, 4, 5, 6, 7, // actual rates
            ]
        );
    }

    #[test]
    fn rates_span_two_elements() {
        let rates: Vec<u8> = (0..(1 + SUPPORTED_RATES_MAX_LEN as u8)).collect();
        let rates_writer = RatesWriter::try_new(&rates[..]).expect("Should be valid RatesWriter");
        let mut buf = vec![];
        rates_writer.write_supported_rates(&mut buf);
        rates_writer.write_ext_supported_rates(&mut buf);
        assert_eq!(
            &buf[..],
            &[
                1, 8, // SupportedRates ID and length
                0, 1, 2, 3, 4, 5, 6, 7, // supp_rates
                50, 1, // ExtendedSupportedRates ID and length
                8, // ext_supp_rates
            ]
        )
    }
}
