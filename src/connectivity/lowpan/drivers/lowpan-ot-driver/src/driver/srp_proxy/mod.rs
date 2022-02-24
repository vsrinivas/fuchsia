// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::prelude::*;
use std::ffi::CStr;

mod discovery_proxy;
pub use discovery_proxy::*;

const LOCAL_DOMAIN: &str = "local.";

/// Placeholder for real TTL values, the addition of which
/// is being tracked by <fxbug.dev/94352>. Value is in seconds.
///
/// The TTL value determines the number of seconds that a returned
/// record can be assumed to be accurate for caching purposes.
///
/// The value 120 was chosen because RFC6762 (page 18) considers
/// this to be a typical TTL value.
const DEFAULT_MDNS_TTL: u32 = 120;

/// Converts an optional vector of strings and to a single string.
fn flatten_txt(txt: Option<Vec<String>>) -> String {
    ot::dnssd_flatten_txt(txt.into_iter().flat_map(IntoIterator::into_iter))
}

/// Splits the TXT record into individual values.
#[allow(dead_code)]
fn split_txt(txt: &[u8]) -> Vec<String> {
    ot::dnssd_split_txt(std::str::from_utf8(txt).unwrap())
        .map(ToString::to_string)
        .collect::<Vec<_>>()
}

fn replace_domain<T: AsRef<str>>(
    hostname: T,
    mut expected_domain: &str,
    new_domain: &str,
) -> Result<String> {
    let mut hostname = hostname.as_ref();

    if hostname.ends_with('.') && !expected_domain.ends_with('.') {
        // Trim trailing period from hostname.
        hostname = &hostname[..hostname.len() - 1];
    }

    if !hostname.ends_with('.') && expected_domain.ends_with('.') {
        // Trim trailing period from expected domain.
        expected_domain = &expected_domain[..expected_domain.len() - 1];
    }

    if !hostname.ends_with(expected_domain) {
        bail!("{:?} is not in domain {:?}", hostname, expected_domain);
    }

    // Trim expected domain.
    hostname = &hostname[..hostname.len() - expected_domain.len()];

    Ok(hostname.to_string() + new_domain)
}

fn replace_domain_cstr<T: AsRef<CStr>>(
    hostname: T,
    expected_domain: &str,
    new_domain: &str,
) -> Result<String> {
    replace_domain(hostname.as_ref().to_str()?, expected_domain, new_domain)
}

#[cfg(test)]
mod test {
    use super::*;

    #[test]
    fn test_replace_domain() {
        assert_eq!(
            replace_domain("hostname.default.service.arpa.", "default.service.arpa.", LOCAL_DOMAIN)
                .unwrap(),
            "hostname.local."
        );
        assert_eq!(
            replace_domain("hostname.default.service.arpa.", "default.service.arpa", LOCAL_DOMAIN)
                .unwrap(),
            "hostname.local."
        );
        assert_eq!(
            replace_domain("hostname.default.service.arpa", "default.service.arpa.", LOCAL_DOMAIN)
                .unwrap(),
            "hostname.local."
        );
        assert_eq!(
            replace_domain("default.service.arpa.", "default.service.arpa.", LOCAL_DOMAIN).unwrap(),
            "local."
        );
        assert!(replace_domain("hostname.default", "default.service.arpa.", LOCAL_DOMAIN).is_err());
        assert!(replace_domain("", "default.service.arpa.", LOCAL_DOMAIN).is_err());
    }
}
