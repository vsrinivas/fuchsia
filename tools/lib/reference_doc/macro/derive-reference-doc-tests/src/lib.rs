// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Allow dead code to avoid annotating every field in the test structs
// with #[allow(dead_code)]
#![allow(dead_code)]

mod tests {
    use {
        pretty_assertions::assert_eq,
        reference_doc::{MarkdownReferenceDocGenerator, ReferenceDoc},
        std::collections::HashMap,
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

        /// This is a vector with overridden json type number.
        #[reference_doc(json_type = "number")]
        r#use: Vec<String>,

        /// This is a map!
        map: Option<HashMap<String, String>>,

        /// This will appear first when describing `objects`.
        #[reference_doc(recurse)]
        objects: Option<Vec<ReferenceDocTestObject1>>,

        /// More objects.
        #[reference_doc(recurse)]
        objects2: ReferenceDocTestObject2,
    }

    /// This will appear after describing the fields.
    #[derive(ReferenceDoc)]
    #[reference_doc(top_level_doc_after_fields)]
    struct ReferenceDocTestObject1 {
        /// This will appear when documenting this field specifically.
        ///
        /// # A super indented header!
        an_int: i32,
    }

    /// This should display fields as a list.
    #[derive(ReferenceDoc)]
    #[reference_doc(fields_as = "list")]
    struct ReferenceDocTestObject2 {
        /// It's a string, but it can take the following values:
        /// - "0": zero things
        /// - "1": one thing!
        val: String,

        /// An enum.
        val2: Option<ReferenceDocTestEnum>,
    }

    enum ReferenceDocTestEnum {
        Foo,
        Bar,
    }

    #[test]
    fn test_reference_doc() {
        assert_eq!(
            &ReferenceDocTest::get_reference_doc_markdown(),
            r#"# Big picture

A struct that is parsed to JSON.

## Top-level keys

### `str` {#str}

_`string` (optional)_

This content describes str.

#### This heading will be indented by default

### `use` {#use}

_array of `number`_

This is a vector with overridden json type number.

### `map` {#map}

_`object` (optional)_

This is a map!

### `objects` {#objects}

_array of `object` (optional)_

This will appear first when describing `objects`.

#### `an_int` {#an_int}

_`number`_

This will appear when documenting this field specifically.

##### A super indented header!


This will appear after describing the fields.



### `objects2` {#objects2}

_`object`_

More objects.

This should display fields as a list.

- `val`: (_`string`_) It's a string, but it can take the following values:
  - "0": zero things
  - "1": one thing!
- `val2`: (_optional `string`_) An enum.


"#,
        );
    }
}
