// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    epoch::EpochFile,
    std::io::{BufRead, BufReader},
    thiserror::Error,
};

/// Wrapper for parsing epoch history.
#[cfg_attr(test, derive(Debug, PartialEq))]
pub struct History {
    pub epoch: u64,
}

#[derive(Error, Debug)]
#[cfg_attr(test, derive(PartialEq))]
pub enum ParseError {
    #[error("line not in form <epoch>=<context>: {0}")]
    Format(String),

    #[error("failed to parse epoch as u64 on line: {0}")]
    IntParse(String),

    #[error("found decreasing epoch on line: {0}")]
    Decreasing(String),

    #[error("found duplicate epoch on line: {0}")]
    Duplicate(String),

    #[error("history must not be empty")]
    Empty,
}

impl std::str::FromStr for History {
    type Err = ParseError;
    fn from_str(s: &str) -> Result<Self, Self::Err> {
        let mut lines = BufReader::new(s.as_bytes()).lines();

        let mut epoch = None;
        while let Some(line) = lines.next() {
            let line = line.expect("read success");
            let as_vec: Vec<String> = line.splitn(2, '=').map(|s| s.to_owned()).collect();

            // Ignore blank lines.
            if as_vec[0] == "" {
                continue;
            }

            // Reject missing `=` or context.
            if as_vec.len() == 1 || as_vec[1] == "" {
                return Err(ParseError::Format(line));
            }

            let curr = match as_vec[0].parse::<u64>() {
                Ok(curr) => curr,
                Err(_) => return Err(ParseError::IntParse(line)),
            };

            if let Some(prev) = epoch {
                if curr == prev {
                    return Err(ParseError::Duplicate(line));
                } else if curr < prev {
                    return Err(ParseError::Decreasing(line));
                }
            }
            epoch = Some(curr);
        }

        Ok(History { epoch: epoch.ok_or_else(|| ParseError::Empty)? })
    }
}

impl Into<EpochFile> for History {
    fn into(self) -> EpochFile {
        let Self { epoch } = self;
        EpochFile::Version1 { epoch }
    }
}

#[cfg(test)]
mod test {
    use {super::*, indoc::indoc, proptest::prelude::*};

    #[test]
    fn success() {
        let data = indoc! {"
            0=zero
            1=one
            2=two
        "};

        let res: Result<History, ParseError> = data.parse();

        assert_eq!(res, Ok(History { epoch: 2 }));
    }

    #[test]
    fn success_skip_epoch() {
        // Deliberately skip 1 to show it's still a valid history.
        let data = indoc! {"
            0=zero
            2=two
        "};

        let res: Result<History, ParseError> = data.parse();

        assert_eq!(res, Ok(History { epoch: 2 }));
    }

    #[test]
    fn success_blank_lines() {
        let data = indoc! {"
            0=zero

            1=one

        "};

        let res: Result<History, ParseError> = data.parse();

        assert_eq!(res, Ok(History { epoch: 1 }));
    }

    #[test]
    fn failure_int_parse() {
        let data = indoc! {"
            0=zero
            z=one
        "};

        let res: Result<History, ParseError> = data.parse();

        assert_eq!(res, Err(ParseError::IntParse("z=one".to_owned())));
    }

    #[test]
    fn failure_duplicate() {
        let data = indoc! {"
            0=zero
            0=one
        "};

        let res: Result<History, ParseError> = data.parse();

        assert_eq!(res, Err(ParseError::Duplicate("0=one".to_owned())));
    }

    #[test]
    fn failure_decreasing() {
        let data = indoc! {"
            0=zero
            1=one
            0=two
        "};

        let res: Result<History, ParseError> = data.parse();

        assert_eq!(res, Err(ParseError::Decreasing("0=two".to_owned())));
    }

    #[test]
    fn failure_empty() {
        let data = "";

        let res: Result<History, ParseError> = data.parse();

        assert_eq!(res, Err(ParseError::Empty));
    }

    #[test]
    fn failure_format_missing_context() {
        let data = indoc! {"
            0=zero
            1=
        "};

        let res: Result<History, ParseError> = data.parse();

        assert_eq!(res, Err(ParseError::Format("1=".to_owned())));
    }

    #[test]
    fn failure_format_epoch_only() {
        let data = indoc! {"
            0=zero
            1
        "};

        let res: Result<History, ParseError> = data.parse();

        assert_eq!(res, Err(ParseError::Format("1".to_owned())));
    }

    proptest! {
        #[test]
        fn history_into_epoch(epoch: u64) {
            let file: EpochFile = History{epoch}.into();
            assert_eq!(file, EpochFile::Version1{epoch})
        }
    }
}
