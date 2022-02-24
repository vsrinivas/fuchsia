// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Allow dead code to avoid annotating every field in the test structs
// with #[allow(dead_code)]
#![allow(dead_code)]

#[cfg(test)]
mod tests {
    use {
        cml::reference_doc::MarkdownReferenceDocGenerator, cml_macro::ReferenceDoc,
        difference::assert_diff,
    };

    /// # Big picture
    ///
    /// A struct that is parsed to JSON.
    ///
    /// ## Top-level keys
    #[derive(ReferenceDoc)]
    struct ReferenceDocTest {
        /// This content describes str.
        ///
        /// # This heading will be indented by default
        str: Option<String>,

        /// This is a vector.
        r#use: Vec<String>,

        /// This will appear first when describing `objects`.
        #[reference_doc(recurse)]
        objects: Option<Vec<ReferenceDocTestObject>>,
    }

    /// This will appear before describing the fields.
    #[derive(ReferenceDoc)]
    struct ReferenceDocTestObject {
        /// This will appear when documenting this field specifically.
        ///
        /// # A super indented header!
        an_int: i32,
    }

    #[test]
    fn test_reference_doc() {
        assert_diff!(
            &ReferenceDocTest::get_reference_doc_markdown(),
            r#"# Big picture

A struct that is parsed to JSON.

## Top-level keys

### `str` {#str}

_`string` (optional)_

This content describes str.

#### This heading will be indented by default

### `use` {#use}

_array of `strings`_

This is a vector.

### `objects` {#objects}

_array of `objects` (optional)_

This will appear first when describing `objects`.

This will appear before describing the fields.

#### `an_int` {#an_int}

_`object`_

This will appear when documenting this field specifically.

##### A super indented header!



"#,
            "",
            0
        );
    }
}
