// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/// Trait added by using `derive(ReferenceDoc)]`.
pub trait MarkdownReferenceDocGenerator {
    /// Returns a Markdown representation of the reference docs for the
    /// struct that is derived from `ReferenceDoc`. The returned Markdown
    /// indents any `#` Markdown headers in individual field doc comments
    /// to ensure a well structured final Markdown document.
    fn get_reference_doc_markdown() -> String;
}
