// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::Error;

enum CurrentSegment {
    Empty,
    Dot,
    DotDot,
    Other,
}
use CurrentSegment::*;

fn error_string(name: &[u8]) -> String {
    String::from_utf8_lossy(name).into_owned()
}

/// Does not validate max name length of 2**16 - 1, as this is not needed on the read path and can
/// be done separately conveniently on the write path.
pub fn validate_name(name: &[u8]) -> Result<&[u8], Error> {
    let mut state = Empty;
    for c in name.iter() {
        state = match c {
            b'\0' => return Err(Error::NameContainsNull(error_string(name))),
            b'/' => match state {
                Empty => {
                    if name[0] == b'/' {
                        return Err(Error::NameStartsWithSlash(error_string(name)));
                    }
                    return Err(Error::NameContainsEmptySegment(error_string(name)));
                }
                Dot => return Err(Error::NameContainsDotSegment(error_string(name))),
                DotDot => return Err(Error::NameContainsDotDotSegment(error_string(name))),
                Other => Empty,
            },
            b'.' => match state {
                Empty => Dot,
                Dot => DotDot,
                DotDot | Other => Other,
            },
            _ => Other,
        }
    }

    match state {
        Empty => {
            if name.is_empty() {
                Err(Error::ZeroLengthName)
            } else {
                Err(Error::NameEndsWithSlash(error_string(name)))
            }
        }
        Dot => Err(Error::NameContainsDotSegment(error_string(name))),
        DotDot => Err(Error::NameContainsDotDotSegment(error_string(name))),
        Other => Ok(name),
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use matches::assert_matches;

    #[test]
    fn valid_names() {
        for name in
            ["a", "a/a", "a/a/a", ".a", "a.", "..a", "a..", "a./a", "a../a", "a/.a", "a/..a"].iter()
        {
            assert_matches!(validate_name(name.as_bytes()), Ok(n) if n == name.as_bytes());
        }
    }

    #[test]
    fn invalid_names() {
        assert_matches!(validate_name(b""), Err(Error::ZeroLengthName));

        for name in ["/", "/a"].iter() {
            assert_matches!(
                validate_name(name.as_bytes()),
                Err(Error::NameStartsWithSlash(n)) if n == error_string(name.as_bytes())
            );
        }

        for name in ["a/", "aa/"].iter() {
            assert_matches!(
                validate_name(name.as_bytes()),
                Err(Error::NameEndsWithSlash(n)) if n == error_string(name.as_bytes())
            );
        }

        for name in ["\0", "a\0", "\0a", "a/\0", "\0/a"].iter() {
            assert_matches!(
                validate_name(name.as_bytes()),
                Err(Error::NameContainsNull(n)) if n == error_string(name.as_bytes())
            );
        }

        for name in ["a//a", "a/a//a"].iter() {
            assert_matches!(
                validate_name(name.as_bytes()),
                Err(Error::NameContainsEmptySegment(n)) if n == error_string(name.as_bytes())
            );
        }

        for name in [".", "./a", "a/.", "a/./a"].iter() {
            assert_matches!(
                validate_name(name.as_bytes()),
                Err(Error::NameContainsDotSegment(n)) if n == error_string(name.as_bytes())
            );
        }

        for name in ["..", "../a", "a/..", "a/../a"].iter() {
            assert_matches!(
                validate_name(name.as_bytes()),
                Err(Error::NameContainsDotDotSegment(n)) if n == error_string(name.as_bytes())
            );
        }
    }
}
