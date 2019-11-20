// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::{borrow::Cow, mem::replace};

#[derive(Clone, Debug, PartialEq, Eq)]
pub(crate) struct Linealyzer {
    state: LinealyzerState,
}

#[derive(Clone, Debug, PartialEq, Eq)]
enum LinealyzerState {
    PartialLine(Vec<u8>),
    ActiveCarriageReturn,
    Empty,
}
use LinealyzerState::{ActiveCarriageReturn, Empty, PartialLine};

/// Splits byte slices into complete lines, where a (possibly empty) line
/// ends (greedily) in `b'\r'`, `b'\n'`, or `b"\r\n"`.
///
/// Copies any remaining bytes after the last line terminal so that a line split across
/// multiple calls to `feed` will be fully returned by the first call to `feed`
/// that ends the line.
///
/// The iterator returned by `feed` should be completely consumed before calling
/// `feed` again.
impl Linealyzer {
    pub(crate) fn new() -> Self {
        Self { state: Empty }
    }

    pub(crate) fn feed<'l, 'a>(&'l mut self, incoming: &'a [u8]) -> LinealyzerIter<'l, 'a> {
        LinealyzerIter { state: &mut self.state, incoming }
    }
}

#[derive(Debug)]
#[must_use]
pub(crate) struct LinealyzerIter<'l, 'a> {
    state: &'l mut LinealyzerState,
    incoming: &'a [u8],
}

impl<'l, 'a> Iterator for LinealyzerIter<'l, 'a> {
    type Item = Cow<'a, [u8]>;

    fn next(&mut self) -> Option<Self::Item> {
        if self.state == &ActiveCarriageReturn && self.incoming.starts_with(b"\n") {
            self.incoming = &self.incoming[1..];
            *self.state = Empty;
        }
        if self.incoming.is_empty() {
            return None;
        }

        if let Some(i) = self.incoming.iter().position(|b| *b == b'\r' || *b == b'\n') {
            let line_end = &self.incoming[..i];
            let ret = match self.state {
                PartialLine(ref mut line_begin) => {
                    let mut line = replace(line_begin, vec![]);
                    line.extend_from_slice(line_end);
                    line.into()
                }
                ActiveCarriageReturn | Empty => line_end.into(),
            };
            if self.incoming[i] == b'\r' {
                *self.state = ActiveCarriageReturn;
            } else {
                *self.state = Empty;
            }
            self.incoming = &self.incoming[i + 1..];
            return Some(ret);
        } else {
            match self.state {
                PartialLine(ref mut partial) => {
                    partial.extend_from_slice(self.incoming);
                }
                ActiveCarriageReturn | Empty => {
                    *self.state = PartialLine(self.incoming.to_vec());
                }
            }
            self.incoming = b"";
            return None;
        }
    }
}

impl<'l, 'a> std::iter::FusedIterator for LinealyzerIter<'l, 'a> {}

#[cfg(test)]
mod tests {
    use {super::*, matches::assert_matches, proptest::prelude::*};

    fn assert_all_3_partitionings_owned(input: &[u8], expected: Vec<Vec<u8>>) {
        for i in 0..=input.len() {
            for j in i..=input.len() {
                let mut linealyzer = Linealyzer::new();
                let mut lines = vec![];
                lines.extend(linealyzer.feed(&input[..i]).map(|l| l.into_owned()));
                lines.extend(linealyzer.feed(&input[i..j]).map(|l| l.into_owned()));
                lines.extend(linealyzer.feed(&input[j..]).map(|l| l.into_owned()));
                assert_eq!(
                    lines, expected,
                    "all_3_partitionings i: {}, j: {}, input: {:?}",
                    i, j, input
                );
            }
        }
    }

    fn assert_all_3_partitionings(input: &[u8], expected: Vec<&[u8]>) {
        assert_all_3_partitionings_owned(input, expected.into_iter().map(|e| e.to_vec()).collect())
    }

    #[test]
    fn empty_line() {
        assert_all_3_partitionings(b"\n", vec![b""]);
        assert_all_3_partitionings(b"\r", vec![b""]);
        assert_all_3_partitionings(b"\r\n", vec![b""]);
    }

    #[test]
    fn two_empty_lines() {
        assert_all_3_partitionings(b"\r\r", vec![b"", b""]);
        assert_all_3_partitionings(b"\r\r\n", vec![b"", b""]);
        assert_all_3_partitionings(b"\r\n\r", vec![b"", b""]);
        assert_all_3_partitionings(b"\r\n\r\n", vec![b"", b""]);
        assert_all_3_partitionings(b"\r\n\n", vec![b"", b""]);
        assert_all_3_partitionings(b"\n\r", vec![b"", b""]);
        assert_all_3_partitionings(b"\n\r\n", vec![b"", b""]);
        assert_all_3_partitionings(b"\n\n", vec![b"", b""]);
    }

    #[test]
    fn ignore_trailing() {
        assert_all_3_partitionings(b"\na", vec![b""]);
        assert_all_3_partitionings(b"\ra", vec![b""]);
        assert_all_3_partitionings(b"\r\na", vec![b""]);
    }

    #[test]
    fn non_empty_body_resets_active_carriage_return() {
        assert_all_3_partitionings(b"\ra\n", vec![b"", b"a"]);
    }

    #[test]
    fn non_empty_line() {
        assert_all_3_partitionings(b"a\n", vec![b"a"]);
        assert_all_3_partitionings(b"a\r", vec![b"a"]);
        assert_all_3_partitionings(b"a\r\n", vec![b"a"]);
    }

    #[test]
    fn increasing_line_length() {
        assert_all_3_partitionings(
            b"\na\naa\naaa\naaaa\n",
            vec![b"", b"a", b"aa", b"aaa", b"aaaa"],
        );
        assert_all_3_partitionings(
            b"\ra\raa\raaa\raaaa\r",
            vec![b"", b"a", b"aa", b"aaa", b"aaaa"],
        );
        assert_all_3_partitionings(
            b"\r\na\r\naa\r\naaa\r\naaaa\r\n",
            vec![b"", b"a", b"aa", b"aaa", b"aaaa"],
        );
    }

    prop_compose! {
        fn random_line_body_byte()
            (byte in proptest::num::u8::ANY) -> u8
        {
            if byte == b'\r' {
                255u8
            } else if byte == b'\n' {
                254u8
            } else {
                byte
            }
        }
    }

    prop_compose! {
        fn random_line_body()
            (body in proptest::collection::vec(
                random_line_body_byte(), 0..3)) -> Vec<u8>
        {
            body
        }
    }

    prop_compose! {
        fn random_line_terminal()
            (s in proptest::string::bytes_regex("\r|\n|\r\n").unwrap()) -> Vec<u8>
        {
            s
        }
    }

    // Returns n bodies and either n-1 or n terminals
    prop_compose! {
        fn random_bodies_and_terminals()
            (bodies in prop::collection::vec(random_line_body(), 1..4))
            (terminals in prop::collection::vec(
                random_line_terminal(),
                bodies.len()-1..=bodies.len()),
             bodies in Just(bodies))
             -> (Vec<Vec<u8>> , Vec<Vec<u8>>)
        {
            (bodies, terminals)
        }
    }

    // Returns `feed` input and expected lines
    fn generate_test_case(
        mut bodies: Vec<Vec<u8>>,
        terminals: Vec<Vec<u8>>,
    ) -> (Vec<u8>, Vec<Vec<u8>>) {
        let mut content = Vec::with_capacity(32);
        let mut expected_bodies = vec![];
        for i in 0..bodies.len() {
            content.extend_from_slice(&bodies[i]);
            if i < terminals.len() {
                let l = content.len();
                if l == 0 || (l > 0 && !(content[l - 1] == b'\r' && terminals[i][0] == b'\n')) {
                    expected_bodies.push(std::mem::replace(&mut bodies[i], Vec::new()));
                }
                content.extend_from_slice(&terminals[i]);
            }
        }
        (content, expected_bodies)
    }

    proptest! {
        #[test]
        fn random_inputs(
            (bodies, terminals) in random_bodies_and_terminals()) {
            let (content, expected_bodies) = generate_test_case(bodies, terminals);
            assert_all_3_partitionings_owned(&content, expected_bodies);
        }
    }

    #[test]
    fn calling_next_after_none_does_not_affect_linealyzer_state() {
        let mut linealyzer = Linealyzer::new();
        let mut iter = linealyzer.feed(b"a");
        assert_eq!(iter.next(), None);
        assert_eq!(iter.next(), None);
        let mut iter = linealyzer.feed(b"\n");
        assert_matches!(iter.next(), Some(Cow::Owned(v)) if v == b"a".to_vec());
        assert_matches!(iter.next(), None);
        assert_matches!(iter.next(), None);
    }
}
