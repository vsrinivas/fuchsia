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

    /// This method is called internally by the reference doc generator when
    /// recursing to generate documentation for field types.
    fn get_reference_doc_markdown_with_options(indent_headers_by: usize) -> String {
        let doc = Self::get_reference_doc_markdown();
        indent_all_markdown_headers_by(&doc, indent_headers_by)
    }
}
/// Helper function to indent markdown headers in `str` by `n` additional hash
/// marks.
fn indent_all_markdown_headers_by(s: &str, n: usize) -> String {
    if n == 0 {
        s.to_string()
    } else {
        s.split('\n').map(|part| indent_markdown_header_by(part, n)).collect::<Vec<_>>().join("\n")
    }
}

fn indent_markdown_header_by(s: &str, n: usize) -> String {
    if s.starts_with("#") {
        "#".to_string().repeat(n) + &s
    } else {
        s.to_string()
    }
}
