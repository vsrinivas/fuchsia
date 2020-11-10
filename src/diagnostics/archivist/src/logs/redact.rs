// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file

use anyhow::Error;
use regex::{Regex, RegexSet};
use serde::Serialize;
use std::borrow::Cow;

mod serialize;
use serialize::Redacted;

/// A `Redactor` is responsible for removing text patterns that seem like user data in logs.
pub struct Redactor {
    /// Used to determine which regexes match, each pattern has the same index as in `replacements`.
    to_redact: RegexSet,

    /// Used to replace substrings of matching text, each pattern has the same index as in
    /// `to_redact`.
    replacements: Vec<Regex>,
}

const REPLACEMENT: &str = "<REDACTED>";
const KNOWN_BAD_PATTERNS: &[&str] = &[
    // Email stub alice@website.tld
    r"[a-zA-Z0-9]*@[a-zA-Z0-9]*\.[a-zA-Z]*",
    // IPv4 Address
    r"((25[0-5]|(2[0-4]|1{0,1}[0-9]){0,1}[0-9])\.){3,3}(25[0-5]|(2[0-4]|1{0,1}[0-9]){0,1}[0-9])",
    // IPv6
    r"(?:[a-fA-F0-9]{1,4}:){7}[a-fA-F0-9]{1,4}",
    // uuid
    r"[0-9a-fA-F]{8}\b-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-\b[0-9a-fA-F]{12}",
    // mac address
    r"([0-9a-fA-F]{1,2}([\.:-])){5}[0-9a-fA-F]{1,2}",
];

impl Redactor {
    pub fn noop() -> Self {
        Self::new(&[]).unwrap()
    }

    pub fn with_static_patterns() -> Self {
        Self::new(KNOWN_BAD_PATTERNS).unwrap()
    }

    fn new(patterns: &[&str]) -> Result<Self, Error> {
        let replacements = patterns.iter().map(|p| Regex::new(p)).collect::<Result<Vec<_>, _>>()?;
        let to_redact = RegexSet::new(patterns)?;
        Ok(Self { to_redact, replacements })
    }

    /// Replace any instances of this redactor's patterns with the value of [`REPLACEMENT`].
    pub fn redact_text<'t>(&self, text: &'t str) -> Cow<'t, str> {
        let mut redacted = Cow::Borrowed(text);
        for idx in self.to_redact.matches(text) {
            redacted =
                Cow::Owned(self.replacements[idx].replace_all(&redacted, REPLACEMENT).to_string());
        }
        redacted
    }

    /// Returns a wrapper around `item` which implements [`serde::Serialize`], redacting from
    /// any strings in `item`, recursively.
    pub fn redact<'m, 'r, M>(&'r self, item: &'m M) -> Redacted<'m, 'r, M>
    where
        M: ?Sized + Serialize,
    {
        Redacted { inner: item, redactor: self }
    }
}

#[cfg(test)]
mod test {
    use super::*;
    use crate::logs::message::{Message, Severity};
    use diagnostics_data::{LogsField, LogsHierarchy, LogsProperty};
    use fidl_fuchsia_sys_internal::SourceIdentity;

    fn test_message(contents: &str) -> Message {
        Message::new(
            0u64, // time
            Severity::Info,
            0, // size
            0, // dropped_logs
            &SourceIdentity::empty(),
            LogsHierarchy::new(
                "root",
                vec![LogsProperty::String(LogsField::Msg, contents.to_string())],
                vec![],
            ),
        )
    }

    macro_rules! test_redaction {
        ($test_name:ident: $input:expr => $output:expr) => {paste::paste!{
            #[test]
            fn [<redact_ $test_name>] () {
                let noop = Redactor::noop();
                let real = Redactor::with_static_patterns();
                assert_eq!(noop.redact_text($input), $input, "no-op redaction must match input exactly");
                assert_eq!(real.redact_text($input), $output);
            }

            #[test]
            fn [<redact_json_ $test_name>] () {
                let input = test_message($input);
                let output = test_message($output);
                let noop = Redactor::noop();
                let real = Redactor::with_static_patterns();

                let input_json = serde_json::to_string_pretty(&input).unwrap();
                let expected_json = serde_json::to_string_pretty(&output).unwrap();
                let noop_json = serde_json::to_string_pretty(&noop.redact(&input)).unwrap();
                let real_json = serde_json::to_string_pretty(&real.redact(&input)).unwrap();

                assert_eq!(noop_json, input_json, "no-op redaction must match input exactly");
                assert_eq!(real_json, expected_json);
            }
        }};
    }

    test_redaction!(email: "Email: alice@website.tld" => "Email: <REDACTED>");
    test_redaction!(ipv4: "IPv4: 8.8.8.8" => "IPv4: <REDACTED>");
    test_redaction!(ipv6: "IPv6: 2001:503:eEa3:0:0:0:0:30" => "IPv6: <REDACTED>");
    test_redaction!(uuid: "UUID: ddd0fA34-1016-11eb-adc1-0242ac120002" => "UUID: <REDACTED>");
    test_redaction!(mac_address: "MAC address: 00:0a:95:9F:68:16" => "MAC address: <REDACTED>");
    test_redaction!(
        combined: "Combined: Email alice@website.tld, IPv4 8.8.8.8" =>
                  "Combined: Email <REDACTED>, IPv4 <REDACTED>"
    );
}
