// Copyright (c) 2020 Google LLC All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#![deny(missing_docs)]
use {
    crate::{test_error, Json5Format},
    json5format::*,
    maplit::hashmap,
    maplit::hashset,
    std::fs::{self, DirEntry},
    std::io::{self, Read},
    std::path::Path,
    std::path::PathBuf,
};

struct FormatTest<'a> {
    options: Option<FormatOptions>,
    input: &'a str,
    error: Option<&'a str>,
    expected: &'a str,
}

impl<'a> Default for FormatTest<'a> {
    fn default() -> Self {
        FormatTest { options: None, input: "", error: None, expected: "" }
    }
}

fn try_test_format(test: FormatTest<'_>) -> Result<(), Error> {
    let result = match ParsedDocument::from_str(test.input, None) {
        Ok(parsed_document) => {
            let format = match test.options {
                Some(options) => Json5Format::with_options(options)?,
                None => Json5Format::new()?,
            };
            format.to_utf8(&parsed_document)
        }
        Err(actual_error) => Err(actual_error),
    };
    match result {
        Ok(bytes) => {
            let actual_formatted_document = std::str::from_utf8(&bytes).unwrap();
            match test.error {
                Some(expected_error) => {
                    println!("Unexpected formatted result:");
                    println!("===========================");
                    println!("{}", actual_formatted_document);
                    println!("===========================");
                    println!("Expected error: {}", expected_error);
                    Err(test_error!(format!(
                        "Unexpected 'Ok()' result.\n expected: '{}'",
                        expected_error
                    )))
                }
                None => {
                    if actual_formatted_document == test.expected {
                        Ok(())
                    } else {
                        println!("expected:");
                        println!("========");
                        println!("{}", test.expected);
                        println!("========");
                        println!("actual:");
                        println!("======");
                        println!("{}", actual_formatted_document);
                        println!("======");
                        Err(test_error!(format!(
                            "Actual formatted document did not match expected."
                        )))
                    }
                }
            }
        }
        Err(actual_error) => match test.error {
            Some(expected_error) => {
                let actual_error = format!("{}", actual_error);
                if expected_error == actual_error {
                    Ok(())
                } else {
                    println!("expected: {}", expected_error);
                    println!("  actual: {}", actual_error);
                    Err(test_error!("Actual error did not match expected error."))
                }
            }
            None => Err(actual_error),
        },
    }
}

fn test_format(test: FormatTest<'_>) -> Result<(), Error> {
    try_test_format(test).map_err(|e| {
        println!("{}", e);
        e
    })
}

#[test]
fn test_format_simple_objects() {
    test_format(FormatTest {
        input: r##"{ "program": {} }"##,
        expected: r##"{
    program: {},
}
"##,
        ..Default::default()
    })
    .unwrap()
}

#[test]
fn test_format_exponential() {
    test_format(FormatTest {
        input: r##"{ "exponential": 3.14e-8 }"##,
        expected: r##"{
    exponential: 3.14e-8,
}
"##,
        ..Default::default()
    })
    .unwrap()
}

#[test]
fn test_last_scope_is_array() {
    test_format(FormatTest {
        input: r##"{
    program: {},
    expose: [
        {
        }

     /* and this */
    ]
}   // line comment on primary object

        // line comment at the end of the document
        // second line comment

    /* block comment at the end of the document
     * block comment continues.
     * end of block comment at end of doc */
"##,
        expected: r##"{
    program: {},
    expose: [
        {},

        /* and this */
    ],
} // line comment on primary object

// line comment at the end of the document
// second line comment

/* block comment at the end of the document
 * block comment continues.
 * end of block comment at end of doc */
"##,
        ..Default::default()
    })
    .unwrap()
}

#[test]
fn test_comment_block() {
    test_format(FormatTest {
        input: r##"// Copyright or other header
    // goes here
{
    program: {},
    expose: [
    /*
    what happens
    with this 1
    */
      /*
    what happens
    with this 2
      */
      /*
       what happens
       with this 3
      */
      /*
        what happens
        with this 4
      */
        /*
          what happens

          with this 5
        */
        /*

          what happens

          with this 6
        */
      /* what happens
         with this 7
         */
      /* what happens
         with this 8
         and this */
         {
         }

         /* and this */
    ]
    }
        // and end of
        // the doc comment"##,
        expected: r##"// Copyright or other header
// goes here
{
    program: {},
    expose: [
        /*
        what happens
        with this 1
        */

        /*
    what happens
    with this 2
      */

        /*
         what happens
         with this 3
        */

        /*
          what happens
          with this 4
        */

        /*
          what happens

          with this 5
        */

        /*

          what happens

          with this 6
        */

        /* what happens
           with this 7
        */

        /* what happens
           with this 8
           and this */
        {},

        /* and this */
    ],
}

// and end of
// the doc comment
"##,
        ..Default::default()
    })
    .unwrap()
}

#[test]
fn test_end_of_line_comments() {
    test_format(FormatTest {
        input: r##"
{ // not an end-of-line comment
  // because it's not an end of a value

  program: {}, // end of line comment

  expose: [
    "value1",// eol comment
             // is here
    "value2", // eol comment 2
              //
              //
              // is also here
    "value3",  // this end of line comment is followed by a comment that is not vertically aligned
    // so we assume this line comment is not part of the previous end-of-line comment
    /*item4*/"value4", /*item5*/"value5", /*item6*/"value6" // eol comment without comma
                                                            // here also
  ],
  some_object: {
    prop1: // eol comment is not here
      "value1",// eol comment
               // is here
    prop2: "value2", // eol comment 2
                     //
                     //
                     // is also here
    prop3: "value3",  // this end of line comment is followed by a comment that is not vertically aligned
    // so we assume this line comment is not part of the previous end-of-line comment
    prop4: "value4", prop5: "value5", prop6: "value6" // eol comment without comma
                                                      // here also
  },
  children:
  [ // line comment after open brace for "children"
  ],
  use: // line comment for "use"
       // and "use" line comment's second line
  [
  ],
  offer: [
  ], // end of line comment for "offer"
  collections: [
  ], // not just one line but this
     // is a multi-line end of line comment for "collections"
     //
     //   - and should have indentation preserved
     //   - with multiple bullet points
  other: [
  ], /// This doc comment style should still work like any other line
     /// or end-of-line comment
     ///
     ///   - and should also have indentation preserved
     ///   - also with multiple bullet points
}
      // not an end-of-line comment because there is a newline; and end of

      // the doc comment was another break,
      // and the document ends without the required newline"##,
        expected: r##"{
    // not an end-of-line comment
    // because it's not an end of a value
    program: {}, // end of line comment
    expose: [
        "value1", // eol comment
                  // is here
        "value2", // eol comment 2
                  //
                  //
                  // is also here
        "value3", // this end of line comment is followed by a comment that is not vertically aligned

        // so we assume this line comment is not part of the previous end-of-line comment

        /*item4*/
        "value4",

        /*item5*/
        "value5",

        /*item6*/
        "value6", // eol comment without comma
                  // here also
    ],
    some_object: {
        // eol comment is not here
        prop1: "value1", // eol comment
                         // is here
        prop2: "value2", // eol comment 2
                         //
                         //
                         // is also here
        prop3: "value3", // this end of line comment is followed by a comment that is not vertically aligned

        // so we assume this line comment is not part of the previous end-of-line comment
        prop4: "value4",
        prop5: "value5",
        prop6: "value6", // eol comment without comma
                         // here also
    },
    children: [
        // line comment after open brace for "children"
    ],

    // line comment for "use"
    // and "use" line comment's second line
    use: [],
    offer: [], // end of line comment for "offer"
    collections: [], // not just one line but this
                     // is a multi-line end of line comment for "collections"
                     //
                     //   - and should have indentation preserved
                     //   - with multiple bullet points
    other: [], /// This doc comment style should still work like any other line
               /// or end-of-line comment
               ///
               ///   - and should also have indentation preserved
               ///   - also with multiple bullet points
}

// not an end-of-line comment because there is a newline; and end of

// the doc comment was another break,
// and the document ends without the required newline
"##,
        ..Default::default()
    })
    .unwrap()
}

#[test]
fn test_breaks_between_line_comments() {
    test_format(FormatTest {
        input: r##"// Copyright or other header
    // goes here

// Another comment block
// separate from the copyright block.
{

    /// doc comment
    /// is here
    program: {},

    /// another doc comment
        /* and block comment */
    /// and doc comment



    /// and multiple blank lines were above this line comment,
    /// but replaced by one.

    /// more than
    /// two contiguous
    /// line comments
    /// are
    /// here
    ///
    /// including empty line comments

    expose: [ // inside array so not end of line comment
// comment block
        // is here

//comment block
// is here 2

        //comment block
        // is here 3

        // and one more

/* and a block comment
        */
    ],
    children:
    [ // line comment after open brace for "children"
    ],
    use: // line comment for "use"
    [
    ],
    collections: [
    ], // not just one line but this
       // is a multi-line end of line comment for "collections"
       //
       //   - and should have indentation preserved
    offer: [
    ], // end of line comment for "offer"
}
        // and end of

        // the doc comment
        // was another break"##,
        expected: r##"// Copyright or other header
// goes here

// Another comment block
// separate from the copyright block.
{
    /// doc comment
    /// is here
    program: {},

    /// another doc comment

    /* and block comment */

    /// and doc comment

    /// and multiple blank lines were above this line comment,
    /// but replaced by one.

    /// more than
    /// two contiguous
    /// line comments
    /// are
    /// here
    ///
    /// including empty line comments
    expose: [
        // inside array so not end of line comment
        // comment block
        // is here

        //comment block
        // is here 2

        //comment block
        // is here 3

        // and one more

        /* and a block comment
        */
    ],
    children: [
        // line comment after open brace for "children"
    ],

    // line comment for "use"
    use: [],
    collections: [], // not just one line but this
                     // is a multi-line end of line comment for "collections"
                     //
                     //   - and should have indentation preserved
    offer: [], // end of line comment for "offer"
}

// and end of

// the doc comment
// was another break
"##,
        ..Default::default()
    })
    .unwrap()
}

#[test]
fn test_format_sort_and_align_block_comment() {
    test_format(FormatTest {
        options: Some(FormatOptions { sort_array_items: true, ..Default::default() }),
        input: r##"{
    "program": {
        "binary": "bin/session_manager"
    },
    "use": [
        { "runner": "elf" },
        {
            // The Realm service allows session_manager to start components.
            "protocol": "/svc/fuchsia.sys2.Realm",
            "from": "framework",
        },
        {
        /* indented block
           comment:
             * is here
             * ok
        */
            "protocol": [
                "/svc/fuchsia.logger.LogSink",
                "/svc/fuchsia.cobalt.LoggerFactory",
            ],
            "from": "realm",
        },
    ],
}
"##,
        expected: r##"{
    program: {
        binary: "bin/session_manager",
    },
    use: [
        {
            runner: "elf",
        },
        {
            // The Realm service allows session_manager to start components.
            protocol: "/svc/fuchsia.sys2.Realm",
            from: "framework",
        },
        {
            /* indented block
               comment:
                 * is here
                 * ok
            */
            protocol: [
                "/svc/fuchsia.cobalt.LoggerFactory",
                "/svc/fuchsia.logger.LogSink",
            ],
            from: "realm",
        },
    ],
}
"##,
        ..Default::default()
    })
    .unwrap()
}

#[test]
fn test_property_name_formatting() {
    test_format(FormatTest {
        input: r##"{
    unquotedName: 1,
    $_is_ok_$: 2,
    $10million: 3,
    _10_9_8___: 4,
    "remove_quotes_$_123": 5,
    "keep quotes": 6,
    "multi \
line \
is \
valid": 7,
    "3.14159": "pi",
    "with 'quotes'": 9,
    'with "quotes"': 10,
}
"##,
        expected: r##"{
    unquotedName: 1,
    $_is_ok_$: 2,
    $10million: 3,
    _10_9_8___: 4,
    remove_quotes_$_123: 5,
    "keep quotes": 6,
    "multi \
line \
is \
valid": 7,
    "3.14159": "pi",
    "with 'quotes'": 9,
    'with "quotes"': 10,
}
"##,
        ..Default::default()
    })
    .unwrap()
}

#[test]
fn test_parse_error_missing_property_value() {
    test_format(FormatTest {
        input: r##"{
    property: {
        sub_property_1: "value",
        sub_property_2: ,
    }
}
"##,
        error: Some(
            "Parse error: 4:25: Property 'sub_property_2' must have a value before the next \
                 comma-separated property:
        sub_property_2: ,
                        ^",
        ),
        ..Default::default()
    })
    .unwrap();
}

#[test]
fn test_parse_error_missing_property_value_when_closing_object() {
    test_format(FormatTest {
        input: r##"{
    property: {
        sub_property_1: "value",
        sub_property_2:
    }
}
"##,
        error: Some(
            "Parse error: 5:5: Property 'sub_property_2' must have a value before closing an \
                 object:
    }
    ^",
        ),
        ..Default::default()
    })
    .unwrap();
}

#[test]
fn test_parse_error_incomplete_property() {
    test_format(FormatTest {
        input: r##"{
    property: {
        sub_property_1: "value1"
        sub_property_2: "value2",
    }
}
"##,
        error: Some(
            r#"Parse error: 4:9: Properties must be separated by a comma:
        sub_property_2: "value2",
        ^~~~~~~~~~~~~~~"#,
        ),
        ..Default::default()
    })
    .unwrap();

    test_format(FormatTest {
        input: r##"{
    property: {
        sub_property_1:
        sub_property_2: "value2",
    }
}
"##,
        error: Some(
            r#"Parse error: 4:9: Properties must be separated by a comma:
        sub_property_2: "value2",
        ^~~~~~~~~~~~~~~"#,
        ),
        ..Default::default()
    })
    .unwrap();

    test_format(FormatTest {
        input: r##"{
    property: {
        sub_property_1: ,
        sub_property_2: "value2",
    }
}
"##,
        error: Some(
            "Parse error: 3:25: Property 'sub_property_1' must have a value before the next \
                 comma-separated property:
        sub_property_1: ,
                        ^",
        ),
        ..Default::default()
    })
    .unwrap();
}

#[test]
fn test_parse_error_property_name_when_array_value_is_expected() {
    test_format(FormatTest {
        input: r##"{
    property: [
        "item1",
        sub_property_1: "value",
    }
}
"##,
        error: Some(r#"Parse error: 4:9: Invalid Object token found while parsing an Array of 1 item (mismatched braces?):
        sub_property_1: "value",
        ^~~~~~~~~~~~~~~"#),
        ..Default::default()
    })
    .unwrap();
}

#[test]
fn test_parse_error_bad_non_string_primitive() {
    test_format(FormatTest {
        input: r##"{
    non_string_literals: [
        null,
        true,
        false,

        0,
        0.,
        0.0,
        0.000,
        .0,
        .000,
        12345,
        12345.00000,
        12345.67890,
        12345.,
        0.678900,
        .67890,
        1234e5678,
        1234E5678,
        1234e+5678,
        1234E+5678,
        1234e-5678,
        1234E-5678,
        12.34e5678,
        1234.E5678,
        .1234e+5678,
        12.34E+5678,
        1234.e-5678,
        .1234E-5678,
        0xabc123ef,
        0Xabc123EF,
        NaN,
        Infinity,

        -12345,
        -12345.67890,
        -12345.,
        -.67890,
        -1234e5678,
        -1234E5678,
        -1234e+5678,
        -1234E+5678,
        -1234e-5678,
        -1234E-5678,
        -12.34e5678,
        -1234.E5678,
        -.1234e+5678,
        -12.34E+5678,
        -1234.e-5678,
        -.1234E-5678,
        -0xabc123ef,
        -0Xabc123EF,
        -NaN,
        -Infinity,

        +12345,
        +12345.67890,
        +12345.,
        +.67890,
        +1234e5678,
        +1234E5678,
        +1234e+5678,
        +1234E+5678,
        +1234e-5678,
        +1234E-5678,
        +0xabc123ef,
        +0Xabc123EF,
        +NaN,
        +Infinity,

        0x123def,
        123def,
    ]
}
"##,
        error: Some(
            "Parse error: 73:9: Unexpected token:
        123def,
        ^",
        ),
        ..Default::default()
    })
    .unwrap();
}

#[test]
fn test_parse_error_leading_zero() {
    test_format(FormatTest {
        input: r##"{
    non_string_literals: [
        0,
        0.,
        0.0,
        0.000,
        .0,
        .000,
        +0.678900,
        -0.678900,
        -01.67890,
    ]
}
"##,
        error: Some(
            "Parse error: 11:9: Unexpected token:
        -01.67890,
        ^",
        ),
        ..Default::default()
    })
    .unwrap();
}

#[test]
fn test_parse_error_expected_object() {
    test_format(FormatTest {
        input: r##"{
    property: [}
}
"##,
        error: Some(r#"Parse error: 2:16: Invalid Object token found while parsing an Array of 0 items (mismatched braces?):
    property: [}
               ^"#),
        ..Default::default()
    })
    .unwrap();
}

#[test]
fn test_parse_error_expected_array() {
    test_format(FormatTest {
        input: r##"{
    property: {]
}
"##,
        error: Some(r#"Parse error: 2:16: Invalid Array token found while parsing an Object of 0 properties (mismatched braces?):
    property: {]
               ^"#),
        ..Default::default()
    })
    .unwrap();
}

#[test]
fn test_parse_error_mismatched_braces() {
    test_format(FormatTest {
        input: r##"{
    property_1: "value1",
    property_2: "value2","##,
        error: Some(
            r#"Parse error: 3:25: Mismatched braces in the document:
    property_2: "value2",
                        ^"#,
        ),
        ..Default::default()
    })
    .unwrap();
}

#[test]
fn test_property_name_separator_missing() {
    test_format(FormatTest {
        input: r##"{
    property_1 "value1",
}
"##,
        error: Some(
            r#"Parse error: 2:5: Unexpected token:
    property_1 "value1",
    ^"#,
        ),
        ..Default::default()
    })
    .unwrap();
}

#[test]
fn test_parse_error_quoted_property_name_separator_missing() {
    test_format(FormatTest {
        input: r##"{
    "property_1" "value1",
}
"##,
        error: Some(
            r#"Parse error: 2:17: Property name separator (:) missing:
    "property_1" "value1",
                ^"#,
        ),
        ..Default::default()
    })
    .unwrap();
}

#[test]
fn test_parse_error_extra_comma_between_properties() {
    test_format(FormatTest {
        input: r##"{
    property_1: "value1",
    ,
    property_2: "value2",
}
"##,
        error: Some(
            "Parse error: 3:5: Unexpected comma without a preceding property:
    ,
    ^",
        ),
        ..Default::default()
    })
    .unwrap();
}

#[test]
fn test_parse_error_comma_before_first_property() {
    test_format(FormatTest {
        input: r##"{
    ,
    property_1: "value1",
    property_2: "value2",
}
"##,
        error: Some(
            "Parse error: 2:5: Unexpected comma without a preceding property:
    ,
    ^",
        ),
        ..Default::default()
    })
    .unwrap();
}

#[test]
fn test_parse_error_extra_comma_between_array_items() {
    test_format(FormatTest {
        input: r##"[
    "value1",
    ,
    "value2",
]"##,
        error: Some(
            "Parse error: 3:5: Unexpected comma without a preceding array item value:
    ,
    ^",
        ),
        ..Default::default()
    })
    .unwrap();
}

#[test]
fn test_parse_error_comma_before_first_array_item() {
    test_format(FormatTest {
        input: r##"[
    ,
    "value1",
    "value2",
]"##,
        error: Some(
            "Parse error: 2:5: Unexpected comma without a preceding array item value:
    ,
    ^",
        ),
        ..Default::default()
    })
    .unwrap();
}

#[test]
fn test_parse_error_quoted_property_name_and_comma_looks_like_a_value() {
    test_format(FormatTest {
        input: r##"{
    property_1: "value1",
    "value2",
}
"##,
        error: Some(
            r#"Parse error: 3:13: Property name separator (:) missing:
    "value2",
            ^"#,
        ),
        ..Default::default()
    })
    .unwrap();
}

#[test]
fn test_parse_error_value_without_property_name() {
    test_format(FormatTest {
        input: r##"{
    property_1: "value1",
    false,
}
"##,
        error: Some(
            "Parse error: 3:5: Object values require property names:
    false,
    ^~~~~",
        ),
        ..Default::default()
    })
    .unwrap();
}

#[test]
fn test_parse_error_unclosed_string() {
    test_format(FormatTest {
        input: r##"{
    property: "bad quotes',
}
"##,
        error: Some(
            r#"Parse error: 2:16: Unclosed string:
    property: "bad quotes',
               ^"#,
        ),
        ..Default::default()
    })
    .unwrap();
}

#[test]
fn test_parse_error_not_json() {
    test_format(FormatTest {
        input: r##"
# Fuchsia

Pink + Purple == Fuchsia (a new operating system)

## How can I build and run Fuchsia?

See [Getting Started](https://fuchsia.dev/fuchsia-src/getting_started.md).

## Where can I learn more about Fuchsia?

See [fuchsia.dev](https://fuchsia.dev).
"##,
        error: Some(
            r#"Parse error: 2:1: Unexpected token:
# Fuchsia
^"#,
        ),
        ..Default::default()
    })
    .unwrap();
}

#[test]
fn test_options() {
    let options = FormatOptions { ..Default::default() };
    assert_eq!(options.indent_by, 4);
    assert_eq!(options.trailing_commas, true);
    assert_eq!(options.collapse_containers_of_one, false);
    assert_eq!(options.sort_array_items, false);

    let options = FormatOptions {
        indent_by: 2,
        trailing_commas: false,
        collapse_containers_of_one: true,
        sort_array_items: true,
        options_by_path: hashmap! {
            "/*" => hashset! {
                PathOption::PropertyNameOrder(vec![
                    "program",
                    "use",
                    "expose",
                    "offer",
                    "children",
                    "collections",
                    "storage",
                    "facets",
                    "runners",
                    "resolvers",
                    "environments",
                ]),
            },
            "/*/use" => hashset! {
                PathOption::TrailingCommas(false),
                PathOption::CollapseContainersOfOne(false),
                PathOption::SortArrayItems(true),
                PathOption::PropertyNameOrder(vec![
                    "name",
                    "url",
                    "startup",
                    "environment",
                    "durability",
                    "service",
                    "protocol",
                    "directory",
                    "storage",
                    "runner",
                    "resolver",
                    "to",
                    "from",
                    "as",
                    "rights",
                    "subdir",
                    "path",
                    "dependency",
                ]),
            },
            "/*/use/service" => hashset! {
                PathOption::SortArrayItems(true),
            },
        },
        ..Default::default()
    };

    assert_eq!(options.indent_by, 2);
    assert_eq!(options.trailing_commas, false);
    assert_eq!(options.collapse_containers_of_one, true);
    assert_eq!(options.sort_array_items, true);

    let path_options = options
        .options_by_path
        .get("/*/use")
        .expect("Expected to find path options for the given path");
    match path_options
        .get(&PathOption::TrailingCommas(true))
        .expect("Expected to find a PathOption::TrailingCommas setting")
    {
        PathOption::TrailingCommas(trailing_commas) => assert_eq!(*trailing_commas, false),
        _ => panic!("PathOption enum as key should return a value of the same type"),
    };
    match path_options
        .get(&PathOption::CollapseContainersOfOne(true))
        .expect("Expected to find a PathOption::CollapseContainersOfOne setting")
    {
        PathOption::CollapseContainersOfOne(collapsed_container_of_one) => {
            assert_eq!(*collapsed_container_of_one, false)
        }
        _ => panic!("PathOption enum as key should return a value of the same type"),
    };
    match path_options
        .get(&PathOption::SortArrayItems(true))
        .expect("Expected to find a PathOption::SortArrayItems setting")
    {
        PathOption::SortArrayItems(sort_array_items) => assert_eq!(*sort_array_items, true),
        _ => panic!("PathOption enum as key should return a value of the same type"),
    };
    match path_options
        .get(&PathOption::PropertyNameOrder(vec![]))
        .expect("Expected to find a PathOption::PropertyNameOrder setting")
    {
        PathOption::PropertyNameOrder(property_names) => assert_eq!(property_names[1], "url"),
        _ => panic!("PathOption enum as key should return a value of the same type"),
    };
}

#[test]
fn test_duplicated_key_in_subpath_options_is_ignored() {
    let options = FormatOptions {
        options_by_path: hashmap! {
            "/*/use" => hashset! {
                PathOption::TrailingCommas(false),
                PathOption::CollapseContainersOfOne(false),
                PathOption::SortArrayItems(true),
                PathOption::PropertyNameOrder(vec![
                    "name",
                    "url",
                    "startup",
                    "environment",
                    "durability",
                    "service",
                    "protocol",
                    "directory",
                    "storage",
                    "runner",
                    "resolver",
                    "to",
                    "from",
                    "as",
                    "rights",
                    "subdir",
                    "path",
                    "dependency",
                ]),
                PathOption::SortArrayItems(false),
            },
        },
        ..Default::default()
    };

    match options.options_by_path.get("/*/use") {
        Some(path_options) => {
            match path_options.get(&PathOption::TrailingCommas(true)) {
                Some(path_option) => match path_option {
                    PathOption::TrailingCommas(trailing_commas) => {
                        assert_eq!(*trailing_commas, false);
                    }
                    _ => panic!("PathOption enum as key should return a value of the same type"),
                },
                None => panic!("Expected to find a PathOption::TrailingCommas setting"),
            }
            match path_options.get(&PathOption::CollapseContainersOfOne(true)) {
                Some(path_option) => match path_option {
                    PathOption::CollapseContainersOfOne(collapsed_container_of_one) => {
                        assert_eq!(*collapsed_container_of_one, false);
                    }
                    _ => panic!("PathOption enum as key should return a value of the same type"),
                },
                None => panic!("Expected to find a PathOption::CollapseContainersOfOne setting"),
            }
            match path_options.get(&PathOption::SortArrayItems(true)) {
                Some(path_option) => match path_option {
                    PathOption::SortArrayItems(sort_array_items) => {
                        assert_eq!(*sort_array_items, true);
                    }
                    _ => panic!("PathOption enum as key should return a value of the same type"),
                },
                None => panic!("Expected to find a PathOption::SortArrayItems setting"),
            }
            match path_options.get(&PathOption::PropertyNameOrder(vec![])) {
                Some(path_option) => match path_option {
                    PathOption::PropertyNameOrder(property_names) => {
                        assert_eq!(property_names[1], "url");
                    }
                    _ => panic!("PathOption enum as key should return a value of the same type"),
                },
                None => panic!("Expected to find a PathOption::PropertyNamePriorities setting"),
            }
        }
        None => panic!("Expected to find path options for the given path"),
    }
}

#[test]
fn test_format_options() {
    test_format(FormatTest {
        options: Some(FormatOptions {
            collapse_containers_of_one: true,
            sort_array_items: true, // but use options_by_path to turn this off for program args
            options_by_path: hashmap! {
                "/*" => hashset! {
                    PathOption::PropertyNameOrder(vec![
                        "program",
                        "children",
                        "collections",
                        "use",
                        "offer",
                        "expose",
                        "resolvers",
                        "runners",
                        "storage",
                        "environments",
                        "facets",
                    ])
                },
                "/*/program" => hashset! {
                    PathOption::CollapseContainersOfOne(false),
                    PathOption::PropertyNameOrder(vec![
                        "binary",
                        "args",
                    ])
                },
                "/*/program/args" => hashset! {
                    PathOption::SortArrayItems(false),
                },
                "/*/*/*" => hashset! {
                    PathOption::PropertyNameOrder(vec![
                        "name",
                        "url",
                        "startup",
                        "environment",
                        "durability",
                        "service",
                        "protocol",
                        "directory",
                        "resolver",
                        "runner",
                        "storage",
                        "from",
                        "as",
                        "to",
                        "rights",
                        "path",
                        "subdir",
                        "event",
                        "dependency",
                        "extends",
                        "resolvers",
                    ])
                },
            },
            ..Default::default()
        }),
        input: r##"{
    offer: [
        {
            runner: "elf",
        },
        {
            from: "framework",
            to: "#elements",
            protocol: "/svc/fuchsia.sys2.Realm",
        },
        {
            to: "#elements",
            protocol: [
                "/svc/fuchsia.logger.LogSink",
                "/svc/fuchsia.cobalt.LoggerFactory",
            ],
            from: "realm",
        },
    ],
    collections: [
        "elements",
    ],
    use: [
        {
            runner: "elf",
        },
        {
            protocol: "/svc/fuchsia.sys2.Realm",
            from: "framework",
        },
        {
            to: "#elements",
            from: "realm",
            protocol: [
                "/svc/fuchsia.logger.LogSink",
                "/svc/fuchsia.cobalt.LoggerFactory",
            ],
        },
    ],
    children: [
    ],
    program: {
        binary: "bin/session_manager",
    },
}
"##,
        expected: r##"{
    program: {
        binary: "bin/session_manager",
    },
    children: [],
    collections: [ "elements" ],
    use: [
        { runner: "elf" },
        {
            protocol: "/svc/fuchsia.sys2.Realm",
            from: "framework",
        },
        {
            protocol: [
                "/svc/fuchsia.cobalt.LoggerFactory",
                "/svc/fuchsia.logger.LogSink",
            ],
            from: "realm",
            to: "#elements",
        },
    ],
    offer: [
        { runner: "elf" },
        {
            protocol: "/svc/fuchsia.sys2.Realm",
            from: "framework",
            to: "#elements",
        },
        {
            protocol: [
                "/svc/fuchsia.cobalt.LoggerFactory",
                "/svc/fuchsia.logger.LogSink",
            ],
            from: "realm",
            to: "#elements",
        },
    ],
}
"##,
        ..Default::default()
    })
    .unwrap();
}

#[test]
fn test_no_trailing_commas() {
    test_format(FormatTest {
        options: Some(FormatOptions { trailing_commas: false, ..Default::default() }),
        input: r##"{
    offer: [
        {
            runner: "elf",
        },
        {
            from: "framework",
            to: "#elements",
            protocol: "/svc/fuchsia.sys2.Realm",
        },
        {
            to: "#elements",
            protocol: [
                "/svc/fuchsia.logger.LogSink",
                "/svc/fuchsia.cobalt.LoggerFactory",
            ],
            from: "realm",
        },
    ],
    collections: [
        "elements",
    ],
    use: [
        {
            runner: "elf",
        },
        {
            protocol: "/svc/fuchsia.sys2.Realm",
            from: "framework",
        },
        {
            from: "realm",
            to: "#elements",
            protocol: [
                "/svc/fuchsia.logger.LogSink",
                "/svc/fuchsia.cobalt.LoggerFactory",
            ],
        },
    ],
    children: [
    ],
    program: {
        binary: "bin/session_manager",
    },
}
"##,
        expected: r##"{
    offer: [
        {
            runner: "elf"
        },
        {
            from: "framework",
            to: "#elements",
            protocol: "/svc/fuchsia.sys2.Realm"
        },
        {
            to: "#elements",
            protocol: [
                "/svc/fuchsia.logger.LogSink",
                "/svc/fuchsia.cobalt.LoggerFactory"
            ],
            from: "realm"
        }
    ],
    collections: [
        "elements"
    ],
    use: [
        {
            runner: "elf"
        },
        {
            protocol: "/svc/fuchsia.sys2.Realm",
            from: "framework"
        },
        {
            from: "realm",
            to: "#elements",
            protocol: [
                "/svc/fuchsia.logger.LogSink",
                "/svc/fuchsia.cobalt.LoggerFactory"
            ]
        }
    ],
    children: [],
    program: {
        binary: "bin/session_manager"
    }
}
"##,
        ..Default::default()
    })
    .unwrap();
}

#[test]
fn test_collapse_containers_of_one() {
    test_format(FormatTest {
        options: Some(FormatOptions { collapse_containers_of_one: true, ..Default::default() }),
        input: r##"{
    offer: [
        {
            runner: "elf",
        },
        {
            from: "framework",
            to: "#elements",
            protocol: "/svc/fuchsia.sys2.Realm",
        },
        {
            to: "#elements",
            protocol: [
                "/svc/fuchsia.logger.LogSink",
                "/svc/fuchsia.cobalt.LoggerFactory",
            ],
            from: "realm",
        },
    ],
    collections: [
        "elements",
    ],
    use: [
        {
            runner: "elf",
        },
        {
            protocol: "/svc/fuchsia.sys2.Realm",
            from: "framework",
        },
        {
            from: "realm",
            to: "#elements",
            protocol: [
                "/svc/fuchsia.logger.LogSink",
                "/svc/fuchsia.cobalt.LoggerFactory",
            ],
        },
    ],
    children: [
    ],
    program: {
        binary: "bin/session_manager",
    },
}
"##,
        expected: r##"{
    offer: [
        { runner: "elf" },
        {
            from: "framework",
            to: "#elements",
            protocol: "/svc/fuchsia.sys2.Realm",
        },
        {
            to: "#elements",
            protocol: [
                "/svc/fuchsia.logger.LogSink",
                "/svc/fuchsia.cobalt.LoggerFactory",
            ],
            from: "realm",
        },
    ],
    collections: [ "elements" ],
    use: [
        { runner: "elf" },
        {
            protocol: "/svc/fuchsia.sys2.Realm",
            from: "framework",
        },
        {
            from: "realm",
            to: "#elements",
            protocol: [
                "/svc/fuchsia.logger.LogSink",
                "/svc/fuchsia.cobalt.LoggerFactory",
            ],
        },
    ],
    children: [],
    program: { binary: "bin/session_manager" },
}
"##,
        ..Default::default()
    })
    .unwrap();
}

#[test]
fn test_validate_example_in_documentation() {
    test_format(FormatTest {
        options: Some(FormatOptions {
            options_by_path: hashmap! {
                "/*" => hashset! {
                    PathOption::PropertyNameOrder(vec![
                        "name",
                        "address",
                        "contact_options",
                    ]),
                },
                "/*/name" => hashset! {
                    PathOption::PropertyNameOrder(vec![
                        "first",
                        "middle",
                        "last",
                        "suffix",
                    ]),
                },
                "/*/*/*" => hashset! {
                    PathOption::PropertyNameOrder(vec![
                        "work",
                        "home",
                        "other",
                    ]),
                },
                "/*/*/*/work" => hashset! {
                    PathOption::PropertyNameOrder(vec![
                        "phone",
                        "email",
                    ]),
                },
            },
            ..Default::default()
        }),
        input: r##"{
    name: {
        last: "Smith",
        first: "John",
        middle: "Jacob",
    },
    address: {
        city: "Anytown",
        country: "USA",
        state: "New York",
        street: "101 Main Street",
    },
    contact_options: [
        {
            other: {
                email: "volunteering@serviceprojectsrus.org",
            },
            home: {
                email: "jj@notreallygmail.com",
                phone: "212-555-4321",
            },
        },
        {
            home: {
                email: "john.smith@notreallygmail.com",
                phone: "212-555-2222",
            },
            work: {
                email: "john.j.smith@worksforme.gov",
                phone: "212-555-1234",
            },
        },
    ],
}
"##,
        expected: r##"{
    name: {
        first: "John",
        middle: "Jacob",
        last: "Smith",
    },
    address: {
        city: "Anytown",
        country: "USA",
        state: "New York",
        street: "101 Main Street",
    },
    contact_options: [
        {
            home: {
                email: "jj@notreallygmail.com",
                phone: "212-555-4321",
            },
            other: {
                email: "volunteering@serviceprojectsrus.org",
            },
        },
        {
            work: {
                phone: "212-555-1234",
                email: "john.j.smith@worksforme.gov",
            },
            home: {
                email: "john.smith@notreallygmail.com",
                phone: "212-555-2222",
            },
        },
    ],
}
"##,
        ..Default::default()
    })
    .unwrap();
}

#[test]
fn test_parse_error_block_comment_not_closed() {
    test_format(FormatTest {
        input: r##"
/*
    Block comment 1
        *//* first line of
  Unclosed block comment 2
    "##,
        error: Some(
            r#"Parse error: 4:13: Block comment started without closing "*/":
        *//* first line of
            ^"#,
        ),
        ..Default::default()
    })
    .unwrap();
}

#[test]
fn test_parse_error_closing_brace_without_opening_brace() {
    test_format(FormatTest {
        input: r##"]"##,
        error: Some(
            r#"Parse error: 1:1: Closing brace without a matching opening brace:
]
^"#,
        ),
        ..Default::default()
    })
    .unwrap();

    test_format(FormatTest {
        input: r##"

  ]"##,
        error: Some(
            r#"Parse error: 3:3: Closing brace without a matching opening brace:
  ]
  ^"#,
        ),
        ..Default::default()
    })
    .unwrap();

    test_format(FormatTest {
        input: r##"
    }"##,
        error: Some(
            r#"Parse error: 2:5: Invalid Object token found while parsing an Array of 0 items (mismatched braces?):
    }
    ^"#,
        ),
        ..Default::default()
    })
    .unwrap();
}

#[test]
fn test_multibyte_unicode_chars() {
    test_format(FormatTest {
        options: None,
        input: concat!(
            r##"/*


#
"##,
            "\u{0010}",
            r##"*//*
"##,
            "\u{E006F} ",
            r##"
*/"##
        ),
        expected: concat!(
            r##"/*


#
"##,
            "\u{0010}",
            r##"*/

/*
"##,
            "\u{E006F} ",
            r##"*/
"##
        ),
        ..Default::default()
    })
    .unwrap();
}

#[test]
fn test_empty_document() {
    test_format(FormatTest { options: None, input: "", expected: "", ..Default::default() })
        .unwrap();
}

fn visit_dir<F>(dir: &Path, cb: &mut F) -> io::Result<()>
where
    F: FnMut(&DirEntry) -> Result<(), std::io::Error>,
{
    if !dir.is_dir() {
        Err(io::Error::new(
            io::ErrorKind::Other,
            format!("visit_dir called with an invalid path: {:?}", dir),
        ))
    } else {
        for entry in fs::read_dir(dir)? {
            let entry = entry?;
            let path = entry.path();
            if path.is_dir() {
                visit_dir(&path, cb)?;
            } else {
                cb(&entry)?;
            }
        }
        Ok(())
    }
}

/// This test is used, for example, to validate fixes to bugs found by oss_fuzz
/// that may have caused the parser to crash instead of either parsing the input
/// successfully or returning a more graceful parsing error.
///
/// To manually verify test samples, use:
///   cargo test test_parsing_samples_does_not_crash -- --nocapture
///
/// To print the full error message (including the line and pointer to the
/// column), use:
///   JSON5FORMAT_TEST_FULL_ERRORS=1 cargo test test_parsing_samples_does_not_crash -- --nocapture
/// To point to a different samples directory:
///   JSON5FORMAT_TEST_SAMPLES_DIR="/tmp/fuzz_corpus" cargo test test_parsing_samples_does_not_crash
#[test]
fn test_parsing_samples_does_not_crash() -> Result<(), std::io::Error> {
    let mut count = 0;
    let pathbuf = if let Some(samples_dir) = option_env!("JSON5FORMAT_TEST_SAMPLES_DIR") {
        PathBuf::from(samples_dir)
    } else {
        let mut manifest_samples = PathBuf::from(env!("CARGO_MANIFEST_DIR"));
        manifest_samples.push("samples");
        manifest_samples
    };
    visit_dir(pathbuf.as_path(), &mut |entry| {
        count += 1;
        let filename = entry.path().into_os_string().to_string_lossy().to_string();
        let mut buffer = String::new();
        println!("{}. Parsing: {} ...", count, filename);
        if let Err(err) = fs::File::open(&entry.path())?.read_to_string(&mut buffer) {
            println!("Ignoring failure to read the file into a string: {:?}", err);
            return Ok(());
        }
        let result = ParsedDocument::from_string(buffer, Some(filename.clone()));
        match result {
            Ok(_parsed_document) => {
                println!("    ... Success");
                Ok(())
            }
            Err(err @ Error::Parse(..)) => {
                if option_env!("JSON5FORMAT_TEST_FULL_ERRORS") == Some("1") {
                    println!("    ... Handled input error:\n{}", err);
                } else if let Error::Parse(some_loc, message) = err {
                    let loc_string = if let Some(loc) = some_loc {
                        format!(" at {}:{}", loc.line, loc.col)
                    } else {
                        "".to_owned()
                    };
                    let mut first_line = message.lines().next().unwrap();
                    // strip the colon off the end of the first line of a parser error message
                    first_line = &first_line[0..first_line.len() - 1];
                    println!("    ... Handled input error{}: {}", loc_string, first_line);
                }

                // It's OK if the input file is bad, as long as the parser fails
                // gracefully.
                Ok(())
            }
            Err(e) => Err(io::Error::new(io::ErrorKind::Other, e)),
        }
    })
}
