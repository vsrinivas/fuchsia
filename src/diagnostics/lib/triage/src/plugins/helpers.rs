// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::metrics::FileDataFetcher,
    regex::{Match, Regex},
};

/// Analyzes the klog and syslog streams looking for a match to `re` and
/// collects the capture groups matches when `re` matches. Note that optional
/// capture groups that didn't participate in the match are omitted and callers
/// should consider this when making assumptions about the length of the Vec
/// passed to `match_fn`. WrappedMatch structs are in the Vec passed to the
/// the match function because WrappedMatch provides a convenient implementation
/// to turn the match into an &str.
pub fn analyze_logs<'t, F>(inputs: &FileDataFetcher<'t>, re: Regex, mut match_fn: F)
where
    F: FnMut(Vec<WrappedMatch<'t>>),
{
    for line in inputs.klog.lines.iter().chain(inputs.syslog.lines.iter()) {
        match re.captures(line) {
            Some(captures) => {
                let mut parts: Vec<WrappedMatch<'t>> = vec![];
                for capture in captures.iter() {
                    if let Some(c) = capture {
                        parts.push(WrappedMatch(c));
                    }
                }
                match_fn(parts);
            }
            _ => {}
        };
    }
}

pub struct WrappedMatch<'t>(pub Match<'t>);

impl<'t> From<WrappedMatch<'t>> for &'t str {
    fn from(m: WrappedMatch<'t>) -> Self {
        m.0.as_str()
    }
}
