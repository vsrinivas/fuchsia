// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {fidl_fuchsia_input_text as ftext, std::ops::Range};

/// Trait for converting `fidl_fuchsia_input_text::Range` into a Rust `Range`.
pub trait IntoRange {
    fn into_range(&self) -> Range<usize>;
}

impl IntoRange for ftext::Range {
    fn into_range(&self) -> Range<usize> {
        self.start as usize..self.end as usize
    }
}

impl IntoRange for Range<usize> {
    fn into_range(&self) -> Range<usize> {
        self.clone()
    }
}

impl IntoRange for ftext::Selection {
    fn into_range(&self) -> Range<usize> {
        let start = std::cmp::min(self.base, self.extent) as usize;
        let end = std::cmp::max(self.base, self.extent) as usize;
        start..end
    }
}

pub trait IntoFuchsiaTextRange {
    fn into_fuchsia_text_range(&self) -> ftext::Range;
}

impl<R> IntoFuchsiaTextRange for R
where
    R: IntoRange,
{
    fn into_fuchsia_text_range(&self) -> ftext::Range {
        let range = self.into_range();
        ftext::Range { start: range.start as u32, end: range.end as u32 }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn selection_into_range() {
        let selection =
            ftext::Selection { base: 17, extent: 9, affinity: ftext::TextAffinity::Downstream };
        let range = selection.into_range();
        assert_eq!(9..17, range);

        let selection =
            ftext::Selection { base: 9, extent: 17, affinity: ftext::TextAffinity::Downstream };
        let range = selection.into_range();
        assert_eq!(9..17, range);
    }
}
