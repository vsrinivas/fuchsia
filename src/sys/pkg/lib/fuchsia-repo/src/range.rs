// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use hyper::header::HeaderValue;

/// Error returned if the range failed to parse.
#[derive(Debug, thiserror::Error)]
pub enum Error {
    #[error("parse error")]
    Parse,

    #[error("overflow")]
    Overflow,

    #[error("multipart ranges are not supported")]
    MultipartRangesAreUnsupported,

    #[error("unknown values are not supported")]
    UnknownValuesAreNotSupported,
}

/// [Range] denotes a range of requested bytes for a [Resource].
///
/// This mostly matches the semantics of the http Range header according to [RFC-7233], but we
/// only support a single range request, rather than multiple requests.
///
/// [RFC-7233]: https://httpwg.org/specs/rfc7233.html#range.requests
#[derive(Debug, Copy, Clone, PartialEq, Eq)]
pub enum Range {
    /// A range that requests the full range of bytes from a resource.
    Full,

    /// A range that requests a subset of bytes from a resource, `first_byte_pos <= x`.
    From { first_byte_pos: u64 },

    /// A range that requests a subset of bytes from a resource, `first_byte_pos <= x && x <=
    /// last_byte_pos`.
    Inclusive { first_byte_pos: u64, last_byte_pos: u64 },

    /// A range that requests a subset of bytes from the end of the resource, or `total - len <= x`.
    Suffix { len: u64 },
}

impl Range {
    /// Parse an HTTP Range header according to [RFC-7233].
    ///
    /// [RFC-7233]: https://httpwg.org/specs/rfc7233.html#range.requests
    pub fn from_http_range_header(header: &HeaderValue) -> Result<Self, Error> {
        parse_range(header.as_ref())
    }

    pub fn to_http_request_header(&self) -> Option<HeaderValue> {
        let value = match self {
            Range::Full => {
                return None;
            }
            Range::Inclusive { first_byte_pos, last_byte_pos } => {
                format!("bytes={}-{}", first_byte_pos, last_byte_pos)
            }
            Range::From { first_byte_pos } => {
                format!("bytes={}-", first_byte_pos)
            }
            Range::Suffix { len } => {
                format!("bytes=-{}", len)
            }
        };

        // The unwrap should be safe here since HeaderValue only fails if there are
        // non-ascii characters in the string.
        let header =
            HeaderValue::from_str(&value).expect("header to only contain ASCII characters");

        Some(header)
    }
}

/// [ContentLength] denotes the size of a [Resource].
///
/// This matches the semantics of the http Content-Length header according to [RFC-7230].
///
/// [RFC-7230]: https://httpwg.org/specs/rfc7230.html#header.content-length
#[derive(Copy, Clone, Debug, PartialEq, Eq)]
pub struct ContentLength(u64);

impl ContentLength {
    pub fn new(content_len: u64) -> Self {
        Self(content_len)
    }

    /// Parse an HTTP Content-Length header according to [RFC-7230].
    ///
    /// [RFC-7230]: https://httpwg.org/specs/rfc7230.html#header.content-length
    pub fn from_http_content_length_header(header: &HeaderValue) -> Result<Self, Error> {
        let content_len = parse_integer(header.as_ref())?;
        Ok(ContentLength(content_len))
    }

    /// Return the content length as a [u64].
    pub fn as_u64(&self) -> u64 {
        self.0
    }

    pub fn contains_range(&self, range: Range) -> bool {
        match range {
            Range::Full => true,
            Range::From { first_byte_pos } => first_byte_pos < self.0,
            Range::Inclusive { first_byte_pos, last_byte_pos } => {
                first_byte_pos <= last_byte_pos && first_byte_pos < self.0 && last_byte_pos < self.0
            }
            Range::Suffix { len } => len <= self.0,
        }
    }
}

/// [ContentRange] denotes the size of a [Resource].
///
/// This mostly matches the semantics of the http Content-Range header according to [RFC-7233], but
/// we require that the complete length of the resource must be known.
///
/// [RFC-7233]: https://httpwg.org/specs/rfc7233.html#header.content-range
#[derive(Copy, Clone, Debug, PartialEq, Eq)]
pub enum ContentRange {
    /// Denotes that the resource contains the full range of bytes.
    Full { complete_len: u64 },

    /// Denotes that the resource contains a partial range of bytes between `start >= x && x <= end`,
    /// inclusive.
    Inclusive { first_byte_pos: u64, last_byte_pos: u64, complete_len: u64 },
}

impl ContentRange {
    /// Parse an HTTP Content-Length header according to [RFC-7230].
    ///
    /// [RFC-7230]: https://httpwg.org/specs/rfc7230.html#header.content-length
    pub fn from_http_content_length_header(header: &HeaderValue) -> Result<Self, Error> {
        Ok(ContentLength::from_http_content_length_header(header)?.into())
    }

    /// Parse an HTTP Content-Range header according to [RFC-7233].
    ///
    /// [RFC-7233]: https://httpwg.org/specs/rfc7233.html#header.content-range
    pub fn from_http_content_range_header(header: &HeaderValue) -> Result<Self, Error> {
        parse_content_range(header.as_ref())
    }

    /// Return the content length of the range, which may be smaller than the total length of the range.
    pub fn content_len(&self) -> u64 {
        match self {
            ContentRange::Full { complete_len } => *complete_len,

            ContentRange::Inclusive { first_byte_pos, last_byte_pos, .. } => {
                if first_byte_pos > last_byte_pos {
                    0
                } else {
                    // Partial is an inclusive range, so we need to add one to compute the total length.
                    let end = last_byte_pos.saturating_add(1);
                    end - first_byte_pos
                }
            }
        }
    }

    /// Return the total length of the range.
    pub fn total_len(&self) -> u64 {
        match self {
            ContentRange::Full { complete_len } | ContentRange::Inclusive { complete_len, .. } => {
                *complete_len
            }
        }
    }

    pub fn to_http_content_range_header(&self) -> Option<HeaderValue> {
        match self {
            ContentRange::Full { .. } => None,
            ContentRange::Inclusive { first_byte_pos, last_byte_pos, complete_len } => {
                let value = format!("bytes {}-{}/{}", first_byte_pos, last_byte_pos, complete_len);

                // The unwrap should be safe here since HeaderValue only fails if there are
                // non-ascii characters in the string.
                let header = HeaderValue::from_str(&value)
                    .expect("header to not contain illegal characters");

                Some(header)
            }
        }
    }
}

impl From<ContentLength> for ContentRange {
    fn from(content_len: ContentLength) -> Self {
        Self::Full { complete_len: content_len.0 }
    }
}

/// Parse a Range header.
///
///     Range                 = byte-ranges-specifier / other-ranges-specifier
///     byte-ranges-specifier = bytes-unit "=" byte-range-set
///     bytes-unit            = "bytes"
///     byte-range-set        = *( "," OWS ) ( byte-range-spec / suffix-byte-range-spec )
///                             *( OWS "," [ OWS ( byte-range-spec / suffix-byte-range-spec ) ] )
fn parse_range(s: &[u8]) -> Result<Range, Error> {
    let s = s.strip_prefix(b"bytes=").ok_or(Error::Parse)?;

    let byte_range_set = s
        .split(|ch| *ch == b',')
        .map(|s| {
            let s = skip_whitespce(s);
            parse_byte_range_spec(s)
        })
        .collect::<Result<Vec<_>, _>>()?;

    // Error out if we did not parse at least one range spec.
    let mut byte_range_set = byte_range_set.into_iter();
    let byte_range_spec = byte_range_set.next().ok_or(Error::Parse)?;

    // FIXME: we only support one range part at the moment.
    if byte_range_set.next().is_some() {
        Err(Error::MultipartRangesAreUnsupported)
    } else {
        Ok(byte_range_spec)
    }
}

/// Parse whitespace.
///
///     OWS = *( SP / HTAB )
fn skip_whitespce(s: &[u8]) -> &[u8] {
    if let Some(pos) = s.iter().position(|ch| *ch != b' ' && *ch != b'\t') {
        &s[pos..]
    } else {
        b""
    }
}

/// Parse a byte range spec.
///
///     byte-range-spec        = first-byte-pos "-" [ last-byte-pos ]
///     suffix-byte-range-spec = "-" suffix-length
///     first-byte-pos         = 1*DIGIT
///     last-byte-pos          = 1*DIGIT
fn parse_byte_range_spec(s: &[u8]) -> Result<Range, Error> {
    let (first_byte_pos, last_byte_pos) = split_once(s, b'-').ok_or(Error::Parse)?;

    if first_byte_pos.is_empty() {
        // If we don't have a first-byte-pos, then we're parsing a suffix-byte-range-spec.
        let last_byte_pos = parse_integer(last_byte_pos)?;

        Ok(Range::Suffix { len: last_byte_pos })
    } else {
        let first_byte_pos = parse_integer(first_byte_pos)?;
        if let Some(last_byte_pos) = parse_optional_integer(last_byte_pos)? {
            Ok(Range::Inclusive { first_byte_pos, last_byte_pos })
        } else {
            Ok(Range::From { first_byte_pos })
        }
    }
}

/// Parse a Content-Range header.
///
///     Content-Range       = byte-content-range
///     byte-content-range  = bytes-unit SP ( byte-range-resp / unsatisfied-range )
///     byte-range-resp     = byte-range "/" ( complete-length / "*" )
///     byte-range          = first-byte-pos "-" last-byte-pos
fn parse_content_range(s: &[u8]) -> Result<ContentRange, Error> {
    let s = s.strip_prefix(b"bytes ").ok_or(Error::Parse)?;
    let (byte_range, complete_len) = split_once(s, b'/').ok_or(Error::Parse)?;
    let (first_byte_pos, last_byte_pos) = split_once(byte_range, b'-').ok_or(Error::Parse)?;

    let first_byte_pos = parse_integer(first_byte_pos)?;
    let last_byte_pos = parse_integer(last_byte_pos)?;

    // We don't support unknown lengths.
    let complete_len = if complete_len == b"*" {
        return Err(Error::UnknownValuesAreNotSupported);
    } else {
        parse_integer(complete_len)?
    };

    Ok(ContentRange::Inclusive { first_byte_pos, last_byte_pos, complete_len })
}

/// Parse an optional integer.
fn parse_optional_integer(s: &[u8]) -> Result<Option<u64>, Error> {
    if s.is_empty() {
        Ok(None)
    } else {
        Ok(Some(parse_integer(s)?))
    }
}

/// Parse an integer.
///
///     1*DIGIT
fn parse_integer(s: &[u8]) -> Result<u64, Error> {
    let mut iter = s.iter();

    // The value requires at least one digit.
    let mut value = match iter.next() {
        Some(ch @ b'0'..=b'9') => (ch - b'0') as u64,
        _ => return Err(Error::Parse),
    };

    for ch in iter {
        match ch {
            ch @ b'0'..=b'9' => {
                let digit = (ch - b'0') as u64;
                value = value
                    .checked_mul(10)
                    .ok_or(Error::Overflow)?
                    .checked_add(digit)
                    .ok_or(Error::Overflow)?;
            }
            _ => return Err(Error::Parse),
        }
    }

    Ok(value)
}

fn split_once(s: &[u8], needle: u8) -> Option<(&[u8], &[u8])> {
    s.iter().position(|ch| *ch == needle).map(|pos| (&s[..pos], &s[pos + 1..]))
}

#[cfg(test)]
mod tests {
    use {super::*, assert_matches::assert_matches};

    #[test]
    fn test_range_parses_correctly() {
        for (header, expected) in [
            ("bytes=1-", Range::From { first_byte_pos: 1 }),
            ("bytes=1-15", Range::Inclusive { first_byte_pos: 1, last_byte_pos: 15 }),
            ("bytes=  \t\t  1-15", Range::Inclusive { first_byte_pos: 1, last_byte_pos: 15 }),
            ("bytes=-15", Range::Suffix { len: 15 }),
        ] {
            let header = HeaderValue::from_static(header);
            let actual = Range::from_http_range_header(&header).unwrap();
            assert_eq!(actual, expected);
        }
    }

    // We don't support multipart ranges.
    #[test]
    fn test_range_does_not_support_multipart_range() {
        let header = HeaderValue::from_static("bytes=1-15, 20-, -50");
        assert_matches!(
            Range::from_http_range_header(&header),
            Err(Error::MultipartRangesAreUnsupported)
        );
    }

    #[test]
    fn test_parse_range_fails_correctly() {
        for header in
            ["", "not-bytes=1-15", "bytes=-", "bytes=", "bytes=A-B", "bytes=1A-2B", "bytes=0x1-0x2"]
        {
            let header = HeaderValue::from_static(header);
            assert_matches!(Range::from_http_range_header(&header), Err(Error::Parse));
        }

        let header = HeaderValue::from_static("bytes=184467440737095516150-184467440737095516151");
        assert_matches!(Range::from_http_range_header(&header), Err(Error::Overflow));
    }

    #[test]
    fn test_range_to_http_range_header() {
        for (range, expected) in [
            (Range::Full, None),
            (Range::From { first_byte_pos: 5 }, Some(HeaderValue::from_static("bytes=5-"))),
            (
                Range::Inclusive { first_byte_pos: 5, last_byte_pos: 10 },
                Some(HeaderValue::from_static("bytes=5-10")),
            ),
            (Range::Suffix { len: 5 }, Some(HeaderValue::from_static("bytes=-5"))),
        ] {
            assert_eq!(range.to_http_request_header(), expected,)
        }
    }

    #[test]
    fn test_content_range_from_http_content_length_parses_correctly() {
        for (header, expected) in [
            ("0", ContentRange::Full { complete_len: 0 }),
            ("1234", ContentRange::Full { complete_len: 1234 }),
        ] {
            let header = HeaderValue::from_static(header);
            let actual = ContentRange::from_http_content_length_header(&header).unwrap();
            assert_eq!(actual, expected);
        }
    }

    #[test]
    fn test_content_range_from_http_content_length_fails_correctly() {
        for header in ["", "abcd", "123abc", "abc123"] {
            let header = HeaderValue::from_static(header);
            assert_matches!(
                ContentRange::from_http_content_length_header(&header),
                Err(Error::Parse)
            );
        }

        let header = HeaderValue::from_static("184467440737095516150");
        assert_matches!(
            ContentRange::from_http_content_length_header(&header),
            Err(Error::Overflow)
        );
    }

    #[test]
    fn test_content_range_from_http_content_range_parses_correctly() {
        for (header, expected) in [(
            "bytes 1-5/10",
            ContentRange::Inclusive { first_byte_pos: 1, last_byte_pos: 5, complete_len: 10 },
        )] {
            let header = HeaderValue::from_static(header);
            let actual = ContentRange::from_http_content_range_header(&header).unwrap();
            assert_eq!(actual, expected);
        }
    }

    // We do not support Content-Range headers with unknown length.
    #[test]
    fn test_content_range_does_not_support_unknown_complete_length() {
        let header = HeaderValue::from_static("bytes 1-15/*");
        assert_matches!(
            ContentRange::from_http_content_range_header(&header),
            Err(Error::UnknownValuesAreNotSupported)
        );
    }

    #[test]
    fn test_content_range_from_http_content_range_fails_correctly() {
        for header in ["", "bytes -/10", "not-bytes 1-5/10", "bytes 0x1-0x2/0x3"] {
            let header = HeaderValue::from_static(header);
            assert_matches!(
                ContentRange::from_http_content_range_header(&header),
                Err(Error::Parse)
            );
        }

        let header = HeaderValue::from_static(
            "bytes 184467440737095516150-184467440737095516151/184467440737095516152",
        );
        assert_matches!(
            ContentRange::from_http_content_range_header(&header),
            Err(Error::Overflow)
        );
    }

    #[test]
    fn test_content_range_content_len() {
        for (range, expected) in [
            (ContentRange::Full { complete_len: 10 }, 10),
            (ContentRange::Inclusive { first_byte_pos: 1, last_byte_pos: 5, complete_len: 10 }, 5),
            (ContentRange::Inclusive { first_byte_pos: 1, last_byte_pos: 1, complete_len: 10 }, 1),
            (ContentRange::Inclusive { first_byte_pos: 5, last_byte_pos: 1, complete_len: 10 }, 0),
        ] {
            assert_eq!(range.content_len(), expected, "{:?}", range);
        }
    }

    #[test]
    fn test_content_range_total_len() {
        for (range, expected) in [
            (ContentRange::Full { complete_len: 10 }, 10),
            (ContentRange::Inclusive { first_byte_pos: 1, last_byte_pos: 5, complete_len: 10 }, 10),
            (ContentRange::Inclusive { first_byte_pos: 1, last_byte_pos: 1, complete_len: 10 }, 10),
            (ContentRange::Inclusive { first_byte_pos: 5, last_byte_pos: 1, complete_len: 10 }, 10),
        ] {
            assert_eq!(range.total_len(), expected, "{:?}", range);
        }
    }

    #[test]
    fn test_content_range_to_http_content_range_header() {
        for (range, expected) in [
            (ContentRange::Full { complete_len: 1234 }, None),
            (
                ContentRange::Inclusive {
                    first_byte_pos: 5,
                    last_byte_pos: 10,
                    complete_len: 1234,
                },
                Some(HeaderValue::from_static("bytes 5-10/1234")),
            ),
        ] {
            assert_eq!(range.to_http_content_range_header(), expected, "{:?}", range);
        }
    }

    #[test]
    fn test_content_length_contains_range_full() {
        assert!(ContentLength::new(0).contains_range(Range::Full));
        assert!(ContentLength::new(100).contains_range(Range::Full));
    }

    #[test]
    fn test_content_length_contains_range_from() {
        assert!(ContentLength::new(1).contains_range(Range::From { first_byte_pos: 0 }));
        assert!(ContentLength::new(100).contains_range(Range::From { first_byte_pos: 50 }));
        assert!(ContentLength::new(100).contains_range(Range::From { first_byte_pos: 99 }));

        assert!(!ContentLength::new(0).contains_range(Range::From { first_byte_pos: 0 }));
        assert!(!ContentLength::new(100).contains_range(Range::From { first_byte_pos: 100 }),);
        assert!(!ContentLength::new(100).contains_range(Range::From { first_byte_pos: 150 }),);
    }

    #[test]
    fn test_content_length_contains_range_inclusive() {
        assert!(ContentLength::new(1)
            .contains_range(Range::Inclusive { first_byte_pos: 0, last_byte_pos: 0 }));
        assert!(ContentLength::new(100)
            .contains_range(Range::Inclusive { first_byte_pos: 0, last_byte_pos: 99 }));
        assert!(ContentLength::new(100)
            .contains_range(Range::Inclusive { first_byte_pos: 50, last_byte_pos: 60 }));

        assert!(!ContentLength::new(0)
            .contains_range(Range::Inclusive { first_byte_pos: 0, last_byte_pos: 0 }));
        assert!(!ContentLength::new(100)
            .contains_range(Range::Inclusive { first_byte_pos: 0, last_byte_pos: 100 }));
        assert!(!ContentLength::new(100)
            .contains_range(Range::Inclusive { first_byte_pos: 95, last_byte_pos: 105 }));
        assert!(!ContentLength::new(100)
            .contains_range(Range::Inclusive { first_byte_pos: 95, last_byte_pos: 105 }));
        assert!(!ContentLength::new(100)
            .contains_range(Range::Inclusive { first_byte_pos: 105, last_byte_pos: 115 }));
    }

    #[test]
    fn test_content_contains_range_suffix() {
        assert!(ContentLength::new(0).contains_range(Range::Suffix { len: 0 }),);
        assert!(ContentLength::new(100).contains_range(Range::Suffix { len: 50 }),);
        assert!(ContentLength::new(100).contains_range(Range::Suffix { len: 100 }),);

        assert!(!ContentLength::new(100).contains_range(Range::Suffix { len: 150 }),);
    }
}
