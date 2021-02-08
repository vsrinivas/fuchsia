// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file

use futures::prelude::*;
use regex::{Error, Regex, RegexSet};
use serde::Serialize;
use std::{borrow::Cow, convert::TryFrom, sync::Arc};

mod serialize;
pub use serialize::{Redacted, RedactedItem};

pub const UNREDACTED_CANARY_MESSAGE: &str = "Log redaction canary: \
    Email: alice@website.tld, \
    IPv4: 8.8.8.8, \
    IPv461: ::ffff:12.34.56.78, \
    IPv462: ::ffff:ab12:cd34, \
    IPv6: 2001:503:eEa3:0:0:0:0:30, \
    IPv6C: fec8::7d84:c1dc:ab34:656a, \
    IPv6LL: fe80::7d84:c1dc:ab34:656a, \
    UUID: ddd0fA34-1016-11eb-adc1-0242ac120002, \
    MAC: de:ad:BE:EF:42:5a";

pub const REDACTED_CANARY_MESSAGE: &str = "Log redaction canary: \
    Email: <REDACTED-EMAIL>, IPv4: <REDACTED-IPV4>, IPv461: ::ffff:<REDACTED-IPV4>, IPv462: ::ffff:<REDACTED-IPV4>, \
    IPv6: <REDACTED-IPV6>, IPv6C: <REDACTED-IPV6>, IPv6LL: fe80::<REDACTED-IPV6-LL>, \
    UUID: <REDACTED-UUID>, MAC: <REDACTED-MAC>";

pub fn emit_canary() {
    tracing::info!("{}", UNREDACTED_CANARY_MESSAGE);
}

/// A `Redactor` is responsible for removing text patterns that seem like user data in logs.
pub struct Redactor {
    /// Used to determine which regexes match, each pattern has the same index as in `replacements`.
    to_redact: RegexSet,

    /// Used to replace substrings of matching text, each pattern has the same index as in
    /// `to_redact`.
    replacements: Vec<PatternReplacer>,
}

struct PatternReplacer {
    matcher: Regex,
    replacement: &'static str,
}

impl TryFrom<&RedactionPattern> for PatternReplacer {
    type Error = Error;

    fn try_from(p: &RedactionPattern) -> Result<Self, Self::Error> {
        Ok(Self { matcher: Regex::new(p.matcher)?, replacement: p.replacement })
    }
}

struct RedactionPattern {
    // A regex to find
    matcher: &'static str,
    // A replacement string for it
    replacement: &'static str,
}

const DEFAULT_REDACTION_PATTERNS: &[RedactionPattern] = &[
    // Email stub alice@website.tld
    RedactionPattern {
        matcher: r"[a-zA-Z0-9]*@[a-zA-Z0-9]*\.[a-zA-Z]*",
        replacement: "<REDACTED-EMAIL>",
    },
    // IPv4 Address
    RedactionPattern {
        matcher: r"((25[0-5]|(2[0-4]|1{0,1}[0-9]){0,1}[0-9])\.){3,3}(25[0-5]|(2[0-4]|1{0,1}[0-9]){0,1}[0-9])",
        replacement: "<REDACTED-IPV4>",
    },
    // Link local IPv6
    RedactionPattern {
        matcher: r"fe80::(?:[a-fA-F0-9]{1,4}:){0,4}[a-fA-F0-9]{1,4}",
        replacement: "fe80::<REDACTED-IPV6-LL>",
    },
    // IPv6 without ::
    RedactionPattern {
        matcher: r"(?:[a-fA-F0-9]{1,4}:){7}[a-fA-F0-9]{1,4}",
        replacement: "<REDACTED-IPV6>",
    },
    // IPv6 with ::
    RedactionPattern {
        matcher: r"(?:[a-fA-F0-9]{1,4}:)+:(?:[a-fA-F0-9]{1,4}:)*[a-fA-F0-9]{1,4}",
        replacement: "<REDACTED-IPV6>",
    },
    // IPv6 starting with :: for ipv4
    RedactionPattern {
        matcher: r"::ffff:[a-fA-F0-9]{1,4}:[a-fA-F0-9]{1,4}",
        replacement: "::ffff:<REDACTED-IPV4>",
    },
    // uuid
    RedactionPattern {
        matcher: r"[0-9a-fA-F]{8}\b-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-\b[0-9a-fA-F]{12}",
        replacement: "<REDACTED-UUID>",
    },
    // mac address
    RedactionPattern {
        matcher: r"([0-9a-fA-F]{1,2}([\.:-])){5}[0-9a-fA-F]{1,2}",
        replacement: "<REDACTED-MAC>",
    },
];

impl Redactor {
    pub fn noop() -> Self {
        Self::new(&[]).unwrap()
    }

    pub fn with_static_patterns() -> Self {
        Self::new(DEFAULT_REDACTION_PATTERNS).unwrap()
    }

    fn new(patterns: &[RedactionPattern]) -> Result<Self, regex::Error> {
        let matchers = patterns.iter().map(|p| p.matcher).collect::<Vec<_>>();
        let to_redact = RegexSet::new(matchers)?;
        let replacements =
            patterns.iter().map(PatternReplacer::try_from).collect::<Result<Vec<_>, _>>()?;
        Ok(Self { to_redact, replacements })
    }

    /// Replace any instances of this redactor's patterns with the value of [`REPLACEMENT`].
    pub fn redact_text<'t>(&self, text: &'t str) -> Cow<'t, str> {
        let mut redacted = Cow::Borrowed(text);
        for idx in self.to_redact.matches(text) {
            redacted = Cow::Owned(
                self.replacements[idx]
                    .matcher
                    .replace_all(&redacted, self.replacements[idx].replacement)
                    .to_string(),
            );
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

    pub fn redact_stream<M: Serialize + 'static>(
        self: &Arc<Self>,
        stream: impl Stream<Item = Arc<M>>,
    ) -> impl Stream<Item = RedactedItem<M>> {
        let redactor = self.clone();
        stream.map(move |inner| RedactedItem { inner, redactor: redactor.clone() })
    }
}

#[cfg(test)]
mod test {
    use super::*;
    use crate::logs::message::{Message, Severity, TEST_IDENTITY};
    use diagnostics_data::{LogsField, LogsHierarchy, LogsProperty};
    use futures::stream::iter as iter2stream;
    use std::sync::Arc;

    fn test_message(contents: &str) -> Message {
        Message::new(
            0u64, // time
            Severity::Info,
            0, // size
            0, // dropped_logs
            &*TEST_IDENTITY,
            LogsHierarchy::new(
                "root",
                vec![LogsProperty::String(LogsField::Msg, contents.to_string())],
                vec![],
            ),
        )
    }

    macro_rules! test_redaction {
        ($($test_name:ident: $input:expr => $output:expr,)+) => {
        paste::paste!{$(
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
        )+}

            #[fuchsia_async::run_singlethreaded(test)]
            async fn redact_all_in_stream() {
                let inputs = vec![$( Arc::new(test_message($input)), )+];
                let outputs = vec![$( Arc::new(test_message($output)), )+];

                let noop = Arc::new(Redactor::noop());
                let real = Arc::new(Redactor::with_static_patterns());

                let input_stream = iter2stream(inputs.clone());
                let noop_stream = noop.redact_stream(iter2stream(inputs.clone()));
                let real_stream = real.redact_stream(iter2stream(inputs.clone()));
                let output_stream = iter2stream(outputs);
                let mut all_streams =
                    input_stream.zip(noop_stream).zip(real_stream).zip(output_stream);

                while let Some((((input, noop), real), output)) = all_streams.next().await {
                    let input_json = serde_json::to_string_pretty(&*input).unwrap();
                    let expected_json = serde_json::to_string_pretty(&*output).unwrap();
                    let noop_json = serde_json::to_string_pretty(&noop).unwrap();
                    let real_json = serde_json::to_string_pretty(&real).unwrap();

                    assert_eq!(noop_json, input_json, "no-op redaction must match input exactly");
                    assert_eq!(real_json, expected_json);
                }
            }
        };
    }

    test_redaction! {
        email: "Email: alice@website.tld" => "Email: <REDACTED-EMAIL>",
        ipv4: "IPv4: 8.8.8.8" => "IPv4: <REDACTED-IPV4>",
        ipv4_in_6: "IPv46: ::ffff:12.34.56.78" => "IPv46: ::ffff:<REDACTED-IPV4>",
        ipv4_in_6_hex: "IPv46h: ::ffff:ab12:34cd" => "IPv46h: ::ffff:<REDACTED-IPV4>",
        ipv6: "IPv6: 2001:503:eEa3:0:0:0:0:30" => "IPv6: <REDACTED-IPV6>",
        ipv6_colon: "IPv6C: [::/0 via fe82::7d84:c1dc:ab34:656a nic 4]" => "IPv6C: [::/0 via <REDACTED-IPV6> nic 4]",
        ipv6_ll: "IPv6LL: fe80::7d84:c1dc:ab34:656a" => "IPv6LL: fe80::<REDACTED-IPV6-LL>",
        uuid: "UUID: ddd0fA34-1016-11eb-adc1-0242ac120002" => "UUID: <REDACTED-UUID>",
        mac_address: "MAC address: 00:0a:95:9F:68:16" => "MAC address: <REDACTED-MAC>",
        combined: "Combined: Email alice@website.tld, IPv4 8.8.8.8" =>
                "Combined: Email <REDACTED-EMAIL>, IPv4 <REDACTED-IPV4>",
        canary: UNREDACTED_CANARY_MESSAGE => REDACTED_CANARY_MESSAGE,
    }
}
