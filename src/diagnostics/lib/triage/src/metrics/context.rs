// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::str::{CharIndices, Chars};

use nom::{
    error::{ErrorKind, ParseError},
    AsBytes, Compare, CompareResult, Err, IResult, InputIter, InputLength, InputTake,
    InputTakeAtPosition, Needed, Offset, ParseTo, Slice,
};

/// Parsing context used to store additional information.
#[derive(Debug, Clone, Copy, PartialEq)]
pub struct ParsingContext<'a> {
    /// The input of the parser.
    input: &'a str,
}

impl<'a> ParsingContext<'a> {
    pub fn new(input: &'a str) -> Self {
        Self { input }
    }
    pub fn into_inner(self) -> &'a str {
        self.input
    }
}

impl<'a> AsBytes for ParsingContext<'a> {
    fn as_bytes(&self) -> &[u8] {
        self.input.as_bytes()
    }
}

impl<'a, T> Compare<T> for ParsingContext<'a>
where
    &'a str: Compare<T>,
{
    fn compare(&self, t: T) -> CompareResult {
        self.input.compare(t)
    }
    fn compare_no_case(&self, t: T) -> CompareResult {
        self.input.compare_no_case(t)
    }
}

impl<'a> InputIter for ParsingContext<'a> {
    type Item = char;
    type Iter = CharIndices<'a>;
    type IterElem = Chars<'a>;
    fn iter_indices(&self) -> Self::Iter {
        self.input.char_indices()
    }
    fn iter_elements(&self) -> Self::IterElem {
        self.input.chars()
    }
    fn position<P>(&self, predicate: P) -> Option<usize>
    where
        P: Fn(Self::Item) -> bool,
    {
        self.input.position(predicate)
    }
    fn slice_index(&self, count: usize) -> Option<usize> {
        self.input.slice_index(count)
    }
}

impl<'a> InputLength for ParsingContext<'a> {
    fn input_len(&self) -> usize {
        self.input.len()
    }
}

impl<'a> InputTake for ParsingContext<'a> {
    fn take(&self, count: usize) -> Self {
        Self::new(&self.input[..count])
    }

    fn take_split(&self, count: usize) -> (Self, Self) {
        let (s0, s1) = self.input.split_at(count);
        (ParsingContext::new(s1), ParsingContext::new(s0))
    }
}

impl<'a, R> Slice<R> for ParsingContext<'a>
where
    &'a str: Slice<R>,
{
    fn slice(&self, range: R) -> Self {
        Self::new(self.input.slice(range))
    }
}

impl<'a> InputTakeAtPosition for ParsingContext<'a> {
    type Item = char;
    fn split_at_position<P, E: ParseError<Self>>(&self, predicate: P) -> IResult<Self, Self, E>
    where
        P: Fn(Self::Item) -> bool,
    {
        self.input
            .position(predicate)
            .map(|idx| Self::take_split(&self, idx))
            .ok_or(Err::Incomplete(Needed::Size(1)))
    }

    fn split_at_position1<P, E: ParseError<Self>>(
        &self,
        predicate: P,
        e: ErrorKind,
    ) -> IResult<Self, Self, E>
    where
        P: Fn(Self::Item) -> bool,
    {
        match self.input.position(predicate) {
            Some(0) => Err(Err::Error(E::from_error_kind(self.clone(), e))),
            Some(idx) => Ok(Self::take_split(&self, idx)),
            None => Err(Err::Incomplete(Needed::Size(1))),
        }
    }

    fn split_at_position_complete<P, E: ParseError<Self>>(
        &self,
        predicate: P,
    ) -> IResult<Self, Self, E>
    where
        P: Fn(Self::Item) -> bool,
    {
        match self.split_at_position(predicate) {
            Err(Err::Incomplete(_)) => Ok(Self::take_split(&self, self.input.input_len())),
            elt => elt,
        }
    }
    fn split_at_position1_complete<P, E: ParseError<Self>>(
        &self,
        predicate: P,
        e: ErrorKind,
    ) -> IResult<Self, Self, E>
    where
        P: Fn(Self::Item) -> bool,
    {
        match self.input.position(predicate) {
            Some(0) => Err(Err::Error(E::from_error_kind(self.clone(), e))),
            Some(idx) => Ok(Self::take_split(&self, idx)),
            None => Ok(Self::take_split(&self, self.input.input_len())),
        }
    }
}

impl<'a> Offset for ParsingContext<'a> {
    fn offset(&self, second: &Self) -> usize {
        self.input.offset(&second.input)
    }
}

impl<'a, R> ParseTo<R> for ParsingContext<'a>
where
    &'a str: ParseTo<R>,
{
    fn parse_to(&self) -> Option<R> {
        self.input.parse_to()
    }
}
