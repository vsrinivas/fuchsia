// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

mod tests {
    use {cml::reference_doc::MarkdownReferenceDocGenerator, cml_macro::ReferenceDoc};

    /// # Top-level heading
    ///
    /// Hello.
    ///
    /// ## Top-level keys
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
        field: Option<String>,

        /// Some more documentation!
        #[allow(dead_code)]
        r#another: Vec<String>,

        /// An optional vector.
        #[allow(dead_code)]
        optionvec: Option<Vec<ReferenceDocTestSubtype>>,
    }

    struct ReferenceDocTestSubtype {
        #[allow(dead_code)]
        hello: i32,
    }

    #[test]
    fn test_reference_doc() {
        assert_eq!(
            ReferenceDocTest::get_reference_doc_markdown(),
            r#"# Top-level heading

Hello.

## Top-level keys

### `field` {#field}

_`string` (optional)_

A field.

- A list
    - A sub-list

#### Heading 1

Content 1

##### Heading 1-2

### `another` {#another}

_array of `strings`_

Some more documentation!

### `optionvec` {#optionvec}

_array of `objects` (optional)_

An optional vector.

"#
        );
    }
}
