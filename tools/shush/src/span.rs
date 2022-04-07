// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use proc_macro2::LineColumn;
use rustfix::diagnostics::DiagnosticSpan;
use std::hash::{Hash, Hasher};

#[derive(Debug, Copy, Clone, PartialEq, Eq, PartialOrd, Ord)]
pub struct Span {
    pub start: LineColumn,
    pub end: LineColumn,
}

// This is needed because the upstream LineColumn type doesn't derive Hash
#[allow(clippy::derive_hash_xor_eq)]
impl Hash for Span {
    fn hash<H: Hasher>(&self, state: &mut H) {
        self.start.line.hash(state);
        self.start.column.hash(state);
        self.end.line.hash(state);
        self.end.column.hash(state);
    }
}

impl Default for Span {
    fn default() -> Self {
        Self {
            start: LineColumn { line: 0, column: 0 },
            end: LineColumn { line: usize::MAX, column: usize::MAX },
        }
    }
}

impl Span {
    /// Checks whether another span is a strict subset of this one
    pub fn contains(&self, other: Span) -> bool {
        let start_before = self.start.line == other.start.line
            && self.start.column <= other.start.column
            || self.start.line < other.start.line;
        let end_after = self.end.line == other.end.line && self.end.column >= other.end.column
            || self.end.line > other.end.line;
        start_before && end_after
    }
}

impl From<proc_macro2::Span> for Span {
    fn from(s: proc_macro2::Span) -> Self {
        let LineColumn { line: line_start, column: column_start } = s.start();
        let LineColumn { line: line_end, column: column_end } = s.end();
        Self {
            start: LineColumn { line: line_start, column: column_start + 1 },
            end: LineColumn { line: line_end, column: column_end + 1 },
        }
    }
}

impl From<&DiagnosticSpan> for Span {
    fn from(s: &DiagnosticSpan) -> Self {
        Self {
            start: LineColumn { line: s.line_start, column: s.column_start },
            end: LineColumn { line: s.line_end, column: s.column_end },
        }
    }
}

#[cfg(test)]
pub(crate) fn span(start: (usize, usize), end: (usize, usize)) -> Span {
    assert!(start.0 < end.0 || start.0 == end.0 && start.1 <= end.1);
    Span {
        start: LineColumn { line: start.0, column: start.1 },
        end: LineColumn { line: end.0, column: end.1 },
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_line_subset() {
        let a = span((5, 1), (5, 10));
        let b = span((4, 1), (6, 10));
        assert!(b.contains(a));
        assert!(!a.contains(b));
    }

    #[test]
    fn test_col_subset() {
        let a = span((3, 2), (3, 9));
        let b = span((3, 1), (3, 10));
        assert!(b.contains(a));
        assert!(!a.contains(b));
    }

    #[test]
    fn test_mixed_subset() {
        let a = span((3, 2), (3, 9));
        let b = span((1, 1), (3, 9));
        assert!(b.contains(a));
        assert!(!a.contains(b));
    }

    #[test]
    fn test_line_overlap() {
        let a = span((3, 2), (4, 9));
        let b = span((3, 1), (3, 10));
        assert!(!b.contains(a));
        assert!(!a.contains(b));
    }

    #[test]
    fn test_col_overlap() {
        let a = span((9, 3), (9, 8));
        let b = span((9, 5), (9, 15));
        assert!(!b.contains(a));
        assert!(!a.contains(b));
    }

    #[test]
    fn test_disparate() {
        let a = span((3, 1), (4, 1));
        let b = span((5, 9), (6, 9));
        assert!(!b.contains(a));
        assert!(!a.contains(b));
    }

    #[test]
    fn test_same() {
        let a = span((5, 1), (5, 10));
        let b = span((5, 1), (5, 10));
        assert!(b.contains(a));
        assert!(a.contains(b));
    }
}
