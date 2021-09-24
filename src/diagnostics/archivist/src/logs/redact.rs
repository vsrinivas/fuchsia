// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file

use crate::ImmutableString;
use futures::prelude::*;
use lazy_static::lazy_static;
use parking_lot::Mutex;
use regex::{Captures, Error, Regex, RegexSet};
use serde::Serialize;
use std::collections::HashMap;
use std::{borrow::Cow, convert::TryFrom, sync::Arc};

mod serialize;
pub use serialize::{Redacted, RedactedItem};

pub const UNREDACTED_CANARY_MESSAGE: &str = "Log redaction canary: \
    Email: alice@website.tld, \
    IPv4: 8.8.8.8, \
    IPv4_New: 8.9.10.42, \
    IPv4_Dup: 8.8.8.8, \
    IPv4_WithPort: 8.8.8.8:8080, \
    IPv461: ::ffff:12.34.56.78, \
    IPv462: ::ffff:ab12:cd34, \
    IPv6: 2001:503:eEa3:0:0:0:0:30, \
    IPv6_WithPort: [2001:503:eEa3:0:0:0:0:30]:8080, \
    IPv6C: fec8::7d84:c1dc:ab34:656a, \
    IPv6LL: fe80::7d84:c1dc:ab34:656a, \
    UUID: ddd0fA34-1016-11eb-adc1-0242ac120002, \
    MAC: de:ad:BE:EF:42:5a, \
    SSID: <ssid-666F6F>, \
    HTTP: http://fuchsia.dev/fuchsia/testing?q=Test, \
    HTTPS: https://fuchsia.dev/fuchsia/testing?q=Test, \
    HEX: 1234567890abcdefABCDEF0123456789, \
    v4Current: 0.1.2.3, \
    v4Loopback: 127.1.2.3, \
    v4LocalAddr: 169.254.12.34, \
    v4LocalMulti: 224.0.0.123, \
    v4Multi: 224.0.1.123, \
    broadcast: 255.255.255.255, \
    v6zeroes: :: ::1, \
    v6LeadingZeroes: ::abcd:dcba:bcde:f, \
    v6TrailingZeroes: f:e:d:c:abcd:dcba:bcde::, \
    v6LinkLocal: feB2:111:222:333:444:555:666:777, \
    v6LocalMulticast: ff72:111:222:333:444:555:666:777, \
    v6Multicast: ff77:111:222:333:444:555:666:777";

// NOTE: The integers in this string are brittle but deterministic. See the comment in the impl
// of Redactor for explanation.
pub const REDACTED_CANARY_MESSAGE: &str = "Log redaction canary: \
    Email: <REDACTED-EMAIL>, IPv4: <REDACTED-IPV4: 1>, IPv4_New: <REDACTED-IPV4: 2>, \
    IPv4_Dup: <REDACTED-IPV4: 1>, IPv4_WithPort: <REDACTED-IPV4: 1>:8080, IPv461: ::ffff:<REDACTED-IPV4: 3>, \
    IPv462: ::ffff:<REDACTED-IPV4: 5>, \
    IPv6: <REDACTED-IPV6: 6>, IPv6_WithPort: [<REDACTED-IPV6: 6>]:8080, IPv6C: <REDACTED-IPV6: 7>, IPv6LL: fe80:<REDACTED-IPV6-LL: 8>, \
    UUID: <REDACTED-UUID>, MAC: de:ad:BE:<REDACTED-MAC: 13>, SSID: <REDACTED-SSID: 14>, \
    HTTP: <REDACTED-URL>, HTTPS: <REDACTED-URL>, HEX: <REDACTED-HEX: 15>, v4Current: 0.1.2.3, \
    v4Loopback: 127.1.2.3, v4LocalAddr: 169.254.12.34, v4LocalMulti: 224.0.0.123, v4Multi: <REDACTED-IPV4: 4>, \
    broadcast: 255.255.255.255, v6zeroes: :: ::1, v6LeadingZeroes: <REDACTED-IPV6: 9>, v6TrailingZeroes: <REDACTED-IPV6: 10>, \
    v6LinkLocal: feB2:<REDACTED-IPV6-LL: 11>, v6LocalMulticast: ff72:111:222:333:444:555:666:777, \
    v6Multicast: ff77:<REDACTED-IPV6-MULTI: 12>";

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

    /// Caches previously seen data to associate with a unique ID.
    ///
    /// Requires a mutex for interior mutability because Redactor is shared between multiple
    /// threads using an Arc.
    redaction_cache: Mutex<RedactionIdCache>,
}

#[derive(Clone, Copy)]
enum MapType {
    // Don't use the map.
    No,
    // Just use and replace the entire match.
    ReplaceAll,
    // Use the whole match to get the id, then replace just the second half.
    Mac,
    // Call a function to handle special-casing the match.
    //   matched: String that was matched by the redaction pattern.
    //   mapper: Maps from redacted strings to anonymous integers.
    //   replacement: Default replacement string-pattern to output.
    //   Returns: String to substitute for the matched string.
    Function(fn(matched: &str, mapper: &Mutex<RedactionIdCache>, replacement: &str) -> String),
}

// Just like a RedactionPattern, but holds the compiled version of the regex.
struct PatternReplacer {
    matcher: Regex,
    replacement: &'static str,
    use_map: MapType,
}

impl TryFrom<&RedactionPattern> for PatternReplacer {
    type Error = Error;

    fn try_from(p: &RedactionPattern) -> Result<Self, Self::Error> {
        let RedactionPattern { replacement, matcher, use_map } = p;
        Ok(Self { matcher: Regex::new(matcher)?, replacement, use_map: *use_map })
    }
}

struct RedactionPattern {
    // A regex to find
    matcher: &'static str,
    // A replacement string for it
    replacement: &'static str,
    // Whether to use RedactionIdCache and inject the small number into `replacement`
    use_map: MapType,
}

struct RedactionIdCache {
    values: HashMap<ImmutableString, ImmutableString>,
}

impl RedactionIdCache {
    fn new() -> Self {
        Self { values: HashMap::new() }
    }

    fn get_id<'a>(&'a mut self, value: &str) -> &'a str {
        if !self.values.contains_key(value) {
            let next_id = self.values.len() + 1;
            self.values
                .insert(value.to_string().into_boxed_str(), next_id.to_string().into_boxed_str());
        }
        self.values.get(value).unwrap()
    }
}

const DEFAULT_REDACTION_PATTERNS: &[RedactionPattern] = &[
    // Email stub alice@website.tld
    RedactionPattern {
        matcher: r"[a-zA-Z0-9]*@[a-zA-Z0-9]*\.[a-zA-Z]*",
        replacement: "<REDACTED-EMAIL>",
        use_map: MapType::No,
    },
    // IPv4 Address
    RedactionPattern {
        matcher: r"\b(((25[0-5]|(2[0-4]|1{0,1}[0-9]){0,1}[0-9])\.){3,3}(25[0-5]|(2[0-4]|1{0,1}[0-9]){0,1}[0-9]))\b",
        replacement: "<REDACTED-IPV4: {}>",
        use_map: MapType::Function(redact_ipv4),
    },
    // First line of matcher: IPv6 without ::
    // Second line: IPv6 with embedded ::
    // Third line: IPv6 starting with :: and 3-7 non-zero fields
    // Fourth line: IPv6 with 3-7 non-zero fields ending with ::
    RedactionPattern {
        matcher: "\\b((?:[[:xdigit:]]{1,4}:){7}[[:xdigit:]]{1,4})\\b|\
        \\b((?:[[:xdigit:]]{1,4}:)+:(?:[[:xdigit:]]{1,4}:)*[[:xdigit:]]{1,4})\\b|\
        ::[[:xdigit:]]{1,4}(:[[:xdigit:]]{1,4}){2,6}\\b|\
        \\b[[:xdigit:]]{1,4}(:[[:xdigit:]]{1,4}){2,6}::",
        replacement: "<REDACTED-IPV6: {}>",
        use_map: MapType::Function(redact_ipv6),
    },
    // uuid
    RedactionPattern {
        matcher: r"[0-9a-fA-F]{8}\b-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-\b[0-9a-fA-F]{12}",
        replacement: "<REDACTED-UUID>",
        use_map: MapType::No,
    },
    // mac address
    RedactionPattern {
        matcher: r"\b(?P<oui>([0-9a-fA-F]{1,2}([\.:-])){3})([0-9a-fA-F]{1,2}([\.:-])){2}[0-9a-fA-F]{1,2}\b",
        replacement: "REDACTED-MAC: ", // MapType::Mac is handled differently
        use_map: MapType::Mac,
    },
    // ssid
    RedactionPattern {
        // The SSID identifier contains at most 32 pairs of hexadecimal characters,
        // but match any number so SSID identifiers with the wrong number of
        // hexadecimal characters are also redacted.
        matcher: r"<ssid-[0-9a-fA-F]*>",
        replacement: "<REDACTED-SSID: {}>",
        use_map: MapType::ReplaceAll,
    },
    // http(s) urls
    RedactionPattern {
        matcher: r#"https?://[^"',;!<> ]*"#,
        replacement: "<REDACTED-URL>",
        use_map: MapType::No,
    },
    // Long hex strings
    RedactionPattern {
        matcher: r#"\b[0-9a-fA-F]{32}\b"#,
        replacement: "<REDACTED-HEX: {}>",
        use_map: MapType::ReplaceAll,
    },
];

// Matchers for use inside special-case IPv4 and IPv6 redaction functions.
// These matchers are used on text that's already known to be IPv4 or IPv6 so they are correct
// without matching trailing wildcards.
lazy_static! {
    // 0.*.*.* = current network (as source)
    // 127.*.*.* = loopback
    // 169.254.*.* = link-local addresses
    // 224.0.0.* = link-local multicast
    static ref CLEARTEXT_IPV4: Regex =
        Regex::new(r#"^0\.|^127\.|^169\.254\.|^224\.0\.0\.|255.255.255.255"#).unwrap();
    // ff.1:** and ff.2:** = local multicast - don't redact
    static ref CLEARTEXT_IPV6: Regex = Regex::new(r#"(?i)^ff[[:xdigit:]][12]:"#).unwrap();
    // ff..:** = multicast - display first 2 bytes and redact
    static ref MULTICAST_IPV6: Regex = Regex::new(r#"(?i)^(ff[[:xdigit:]][[:xdigit:]]):"#).unwrap();
    // fe80/10 = link-local - display first 2 bytes and redact
    static ref LINK_LOCAL_IPV6: Regex = Regex::new(r#"(?i)^(fe[89ab][[:xdigit:]]):"#).unwrap();
    // ::ffff:*:* = IPv4 - redact everything
    static ref IPV4_IN_IPV6: Regex = Regex::new(r#"(?i)^::f{4}(:[[:xdigit:]]{1,4}){2}$"#).unwrap();
}

fn redact_ipv4(ip: &str, redaction_cache: &Mutex<RedactionIdCache>, replacement: &str) -> String {
    if CLEARTEXT_IPV4.is_match(ip) {
        ip.to_string()
    } else {
        let mut cache = redaction_cache.lock();
        let id = cache.get_id(ip);
        replacement.replace("{}", id)
    }
}

fn redact_ipv6(ip: &str, redaction_cache: &Mutex<RedactionIdCache>, replacement: &str) -> String {
    if CLEARTEXT_IPV6.is_match(ip) {
        return ip.to_string();
    }
    let special_redact = if let Some(captures) = MULTICAST_IPV6.captures(ip) {
        Some((captures.get(1).unwrap().as_str(), "REDACTED-IPV6-MULTI"))
    } else if let Some(captures) = LINK_LOCAL_IPV6.captures(ip) {
        Some((captures.get(1).unwrap().as_str(), "REDACTED-IPV6-LL"))
    } else if IPV4_IN_IPV6.is_match(ip) {
        Some(("::ffff", "REDACTED-IPV4"))
    } else {
        None
    };
    let mut cache = redaction_cache.lock();
    let id = cache.get_id(ip);
    if let Some((prefix, label)) = special_redact {
        format!("{}:<{}: {}>", prefix, label, id)
    } else {
        replacement.replace("{}", id)
    }
}

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
        Ok(Self { to_redact, replacements, redaction_cache: Mutex::new(RedactionIdCache::new()) })
    }

    // Note: Each new redacted string is given an increasing number. It's important for testing
    // that the strings are encountered in a predictable sequence.
    // Currently the redacted strings are encountered in this sequence:
    //   - the outer loop iterates over the DEFAULT_REDACTION_PATTERN matchers
    //      - the inner loop goes left to right in the string being edited.

    /// Replace any instances of this redactor's patterns with the value of [`REPLACEMENT`].
    pub fn redact_text<'t>(&self, text: &'t str) -> Cow<'t, str> {
        let mut redacted = Cow::Borrowed(text);
        for idx in self.to_redact.matches(text) {
            let replacer = &self.replacements[idx];
            redacted = Cow::Owned(
                match replacer.use_map {
                    MapType::No => replacer.matcher.replace_all(&redacted, replacer.replacement),
                    MapType::ReplaceAll => {
                        replacer.matcher.replace_all(&redacted, |captures: &'_ Captures<'_>| {
                            let mut cache = self.redaction_cache.lock();
                            let id = cache.get_id(&captures[0]);
                            replacer.replacement.replace("{}", id)
                        })
                    }
                    MapType::Mac => {
                        replacer.matcher.replace_all(&redacted, |captures: &'_ Captures<'_>| {
                            let oui = captures.name("oui");
                            let mut cache = self.redaction_cache.lock();
                            let id = cache.get_id(&captures[0]);
                            format!(
                                "{}<{}{}>",
                                oui.map_or("regex error", |o| o.as_str()),
                                replacer.replacement,
                                id
                            )
                        })
                    }
                    MapType::Function(function) => {
                        replacer.matcher.replace_all(&redacted, |captures: &'_ Captures<'_>| {
                            function(&captures[0], &self.redaction_cache, replacer.replacement)
                        })
                    }
                }
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
    use crate::testing::TEST_IDENTITY;
    use diagnostics_data::Severity;
    use diagnostics_message::Message;
    use futures::stream::iter as iter2stream;
    use pretty_assertions::assert_eq;
    use std::sync::Arc;

    fn test_message(contents: &str) -> Message {
        diagnostics_data::LogsDataBuilder::new(diagnostics_data::BuilderArgs {
            timestamp_nanos: 0.into(),
            component_url: Some(TEST_IDENTITY.url.clone()),
            moniker: TEST_IDENTITY.to_string(),
            severity: Severity::Info,
            size_bytes: 0,
        })
        .set_message(contents.to_string())
        .build()
        .into()
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

        };
    }

    // Each entry in test_redaction uses its own instance of Redactor so all strings restart
    //  numbering at 1. Within a string, numbers may be out of order because they depend on the
    //  order that redact matchers are executed (e.g. long_hex). See the comment in the impl
    //  of Redactor for explanation.
    // MAC contains two slightly different addresses, to verify we map the entire
    //  address and not just the part we replace.
    test_redaction! {
        email: "Email: alice@website.tld" => "Email: <REDACTED-EMAIL>",
        ipv4: "IPv4: 8.8.8.8" => "IPv4: <REDACTED-IPV4: 1>",
        ipv4_in_6: "IPv46: ::ffff:12.34.56.78" => "IPv46: ::ffff:<REDACTED-IPV4: 1>",
        ipv4_in_6_hex: "IPv46h: ::ffff:ab12:34cd" => "IPv46h: ::ffff:<REDACTED-IPV4: 1>",
        not_ipv4_in_6_hex: "not_IPv46h: ::ffff:ab12:34cd:5" => "not_IPv46h: <REDACTED-IPV6: 1>",
        ipv6: "IPv6: 2001:503:eEa3:0:0:0:0:30" => "IPv6: <REDACTED-IPV6: 1>",
        ipv6_colon: "IPv6C: [::/0 via 2082::7d84:c1dc:ab34:656a nic 4]" =>
            "IPv6C: [::/0 via <REDACTED-IPV6: 1> nic 4]",
        ipv6_ll: "IPv6LL: fe80::7d84:c1dc:ab34:656a" => "IPv6LL: fe80:<REDACTED-IPV6-LL: 1>",
        uuid: "UUID: ddd0fA34-1016-11eb-adc1-0242ac120002" => "UUID: <REDACTED-UUID>",
        mac_address: "MAC address: 00:0a:95:9F:68:16 12:34:95:9F:68:16" =>
            "MAC address: 00:0a:95:<REDACTED-MAC: 1> 12:34:95:<REDACTED-MAC: 2>",
        ssid: "SSID: <ssid-666F6F> <ssid-77696669>" =>
            "SSID: <REDACTED-SSID: 1> <REDACTED-SSID: 2>",
        http: "HTTP: http://fuchsia.dev/" =>
            "HTTP: <REDACTED-URL>",
        https: "HTTPS: https://fuchsia.dev/" =>
            "HTTPS: <REDACTED-URL>",
        combined: "Combined: Email alice@website.tld, IPv4 8.8.8.8" =>
                "Combined: Email <REDACTED-EMAIL>, IPv4 <REDACTED-IPV4: 1>",
        preserve: "service::fidl service:fidl" => "service::fidl service:fidl",
        long_hex: "456 1234567890abcdefABCDEF0123456789 1.2.3.4" =>
                  "456 <REDACTED-HEX: 2> <REDACTED-IPV4: 1>",
        ipv4_0: "current: 0.8.8.8" => "current: 0.8.8.8",
        ipv4_127: "loopback: 127.8.8.8" => "loopback: 127.8.8.8",
        ipv4_196254: "link_local: 169.254.8.8" => "link_local: 169.254.8.8",
        ipv4_224: "link_local_multicast: 224.0.0.8" => "link_local_multicast: 224.0.0.8",
        ipv4_broadcast: "broadcast: 255.255.255.255" => "broadcast: 255.255.255.255",
        ipv4_not_broadcast: "not_broadcast: 255.255.255.254" => "not_broadcast: <REDACTED-IPV4: 1>",
        ipv4_not_link_local_multicast: "not_link_local_multicast: 224.0.1.8" => "not_link_local_multicast: <REDACTED-IPV4: 1>",
        ipv6_local_multicast_1: "local_multicast_1: fF41::1234:5678:9aBc" => "local_multicast_1: fF41::1234:5678:9aBc",
        ipv6_local_multicast_2: "local_multicast_2: Ffe2:1:2:33:abcd:ef0:6789:456" => "local_multicast_2: Ffe2:1:2:33:abcd:ef0:6789:456",
        ipv6_multicast_3: "multicast: fF43:abcd::ef0:6789:456" => "multicast: fF43:<REDACTED-IPV6-MULTI: 1>",
        ipv6_fe89: "link_local_8: fe89:123::4567:8:90" => "link_local_8: fe89:<REDACTED-IPV6-LL: 1>",
        ipv6_feb2: "link_local_b: FEB2:123::4567:8:90" => "link_local_b: FEB2:<REDACTED-IPV6-LL: 1>",
        ipv6_fec1: "not_link_local: fec1:123::4567:8:90" => "not_link_local: <REDACTED-IPV6: 1>",
        ipv6_fe71: "not_link_local_2: fe71:123::4567:8:90" => "not_link_local_2: <REDACTED-IPV6: 1>",
        short_colons: "not_address_1: 12:34::" => "not_address_1: 12:34::",
        colons_short: "not_address_2: ::12:34" => "not_address_2: ::12:34",
        colons_3_fields: "v6_colons_3_fields: ::12:34:5" => "v6_colons_3_fields: <REDACTED-IPV6: 1>",
        v6_3_fields_colons: "v6_3_fields_colons: 12:34:5::" => "v6_3_fields_colons: <REDACTED-IPV6: 1>",
        colons_7_fields: "v6_colons_7_fields: ::12:234:35:46:5:6:7" => "v6_colons_7_fields: <REDACTED-IPV6: 1>",
        v6_7_fields_colons: "v6_7_fields_colons: 12:234:35:46:5:6:7::" => "v6_7_fields_colons: <REDACTED-IPV6: 1>",
        colons_8_fields: "v6_colons_8_fields: ::12:234:35:46:5:6:7:8" => "v6_colons_8_fields: <REDACTED-IPV6: 1>:8",
        v6_8_fields_colons: "v6_8_fields_colons: 12:234:35:46:5:6:7:8::" => "v6_8_fields_colons: <REDACTED-IPV6: 1>::",
        canary: UNREDACTED_CANARY_MESSAGE => REDACTED_CANARY_MESSAGE,
    }

    // A single Redactor is used for every line in the stream, so the numbers grow.
    #[fuchsia_async::run_singlethreaded(test)]
    async fn redact_all_in_stream() {
        let data = vec![
            ("Email: alice@website.tld", "Email: <REDACTED-EMAIL>"),
            ("IPv4: 8.8.8.8", "IPv4: <REDACTED-IPV4: 1>"),
            ("IPv46: ::ffff:12.34.56.78", "IPv46: ::ffff:<REDACTED-IPV4: 2>"),
            ("IPv46h: ::ffff:ab12:34cd", "IPv46h: ::ffff:<REDACTED-IPV4: 3>"),
            ("IPv6: 2001:503:eEa3:0:0:0:0:30", "IPv6: <REDACTED-IPV6: 4>"),
            ("IPv4.1: 8.9.10.42", "IPv4.1: <REDACTED-IPV4: 5>"),
            ("IPv4.2: 8.8.8.8", "IPv4.2: <REDACTED-IPV4: 1>"),
            (
                "456 1234567890abcdefABCDEF0123456789 12.34.56.78",
                "456 <REDACTED-HEX: 6> <REDACTED-IPV4: 2>",
            ),
        ];

        let inputs =
            data.iter().map(|(input, _)| Arc::new(test_message(input))).collect::<Vec<_>>();
        let outputs =
            data.iter().map(|(_, output)| Arc::new(test_message(output))).collect::<Vec<_>>();

        let noop = Arc::new(Redactor::noop());
        let real = Arc::new(Redactor::with_static_patterns());

        let input_stream = iter2stream(inputs.clone());
        let noop_stream = noop.redact_stream(iter2stream(inputs.clone()));
        let real_stream = real.redact_stream(iter2stream(inputs.clone()));
        let output_stream = iter2stream(outputs);
        let mut all_streams = input_stream.zip(noop_stream).zip(real_stream).zip(output_stream);

        while let Some((((input, noop), real), output)) = all_streams.next().await {
            let input_json = serde_json::to_string_pretty(&*input).unwrap();
            let expected_json = serde_json::to_string_pretty(&*output).unwrap();
            let noop_json = serde_json::to_string_pretty(&noop).unwrap();
            let real_json = serde_json::to_string_pretty(&real).unwrap();

            assert_eq!(noop_json, input_json, "no-op redaction must match input exactly");
            assert_eq!(real_json, expected_json);
        }
    }
}
