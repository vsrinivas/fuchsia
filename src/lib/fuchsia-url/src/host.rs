// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::errors::ParseError;

// The host of a fuchsia-pkg:// URL.
#[derive(Clone, Debug, PartialEq, Eq, PartialOrd, Ord, Hash)]
pub(crate) struct Host(String);

impl Host {
    /// Returns an error if the provided hostname does not comply to the package URL spec:
    /// https://fuchsia.dev/fuchsia-src/concepts/packages/package_url#repository
    /// Contains only lowercase ascii letters, digits, a hyphen or the dot delimiter.
    pub fn parse(host: String) -> Result<Self, ParseError> {
        url::Host::parse(&host)?;

        if host.is_empty() {
            return Err(ParseError::EmptyHost);
        }

        if !host
            .chars()
            .all(|c| c.is_ascii_lowercase() || c.is_ascii_digit() || c == '-' || c == '.')
        {
            return Err(ParseError::InvalidHost);
        }
        Ok(Self(host))
    }
}

impl AsRef<str> for Host {
    fn as_ref(&self) -> &str {
        &self.0
    }
}

impl From<Host> for String {
    fn from(host: Host) -> Self {
        host.0
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn parse_err() {
        for host in [
            "FuChSiA.CoM",
            "FUCHSIA_1.com",
            "FUCHSIA-1.COM",
            "fuchsia-①.com",
            "RISCV.fuchsia.com",
            "RV64.fuchsia.com",
            "fu_chsia.com",
        ] {
            assert_eq!(
                Host::parse(host.to_string()),
                Err(ParseError::InvalidHost),
                "the host string {:?}",
                host
            );
        }

        for host in ["fu:chsia.com", "fu#chsia.com", "fu?chsia.com", "fu/chsia.com"] {
            assert_eq!(
                Host::parse(host.to_string()),
                Err(ParseError::UrlParseError(url::ParseError::InvalidDomainCharacter)),
                "the host string {:?}",
                host
            );
        }

        assert_eq!(Host::parse("".into()), Err(ParseError::EmptyHost));
    }

    #[test]
    fn parse_ok() {
        for host in ["example.org", "ex.am.ple.org", "example0.org", "ex-ample.org", "a", "1", "."]
        {
            assert_eq!(Host::parse(host.to_string()).unwrap().as_ref(), host);
        }
    }
}
