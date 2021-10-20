// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

mod tests {
    use {cml::reference_doc::MarkdownReferenceDocGenerator, cml_macro::ReferenceDoc};

    /// This is a top-level comment.
    ///
    /// # Top-level heading
    ///
    /// Hello.
    #[derive(ReferenceDoc)]
    struct ReferenceDocTest {
        /// A field.
        ///
        /// - A list
        ///     - A sub-list
        ///
        /// # Heading 1
        ///
        /// Content 1
        ///
        /// ## Heading 1-2
        #[allow(dead_code)]
        field: i32,

        /// Some more documentation!
        #[allow(dead_code)]
        r#another: i32,
    }

    #[test]
    fn test_reference_doc() {
        assert_eq!(
            ReferenceDocTest::get_markdown_reference_docs(),
            r#"# Component manifest (`.cml`) reference

This is a top-level comment.

## Top-level heading

Hello.

## Fields

### `field` {#field}

A field.

- A list
    - A sub-list

#### Heading 1

Content 1

##### Heading 1-2

### `another` {#another}

Some more documentation!

"#
        );
    }
}
