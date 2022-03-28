// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::parser,
    anyhow::{Context, Result},
    handlebars::Handlebars,
    serde::Serialize,
    std::collections::hash_map::DefaultHasher,
    std::collections::HashSet,
    std::hash::{Hash, Hasher},
    std::io,
};

/// One constant:
///
/// ```ignore
/// // comment[0]
/// // comment[1]
/// // ...
/// name = value;
/// ```
#[derive(Serialize, Debug, Clone)]
pub struct Constant {
    name: String,
    value: u64,
    comments: Vec<String>,
}

// Generates a locally unique identifier for [message].  The unique identifier is generated stably
// based on the name and content.  This is not very sophisticated, but for as long as we are
// rebuilding all messages at compile time, this is good enough.
//
// This is needed so that messages that are homoglyphs in English but are not in other languages
// can coexist:
//
// ```
// <!-- читати -->
// <string name="action_read">Read</string>
// <!-- прочитане -->
// <string name="label_read_messages>Read</string>
// ```
pub(crate) fn gen_id(name: impl AsRef<str>, message: impl AsRef<str>) -> u64 {
    let mut hasher = DefaultHasher::new();
    name.as_ref().hash(&mut hasher);
    message.as_ref().hash(&mut hasher);
    hasher.finish()
}

// Splits the given internationalized messages into quoted strings of fixed size.  Used to give
// reasonably formatted usage hint in the generated code.
fn split_message(message: impl AsRef<str>) -> Vec<String> {
    const SLICE: usize = 50;
    // This is not iterating over grapheme clusters, but for now will have
    // to do.
    message
        .as_ref()
        // Split up into individual characters, and slice them up into
        // mostly equally-sized slices.
        .chars()
        .collect::<Vec<char>>()
        .chunks(SLICE)
        // Build up a string from each of such slices.
        .map(|c| c.iter().collect::<String>())
        // Sanitize newlines (since they will mess with code generation)
        .map(|s| format!("'{}'", s.replace("\n", " ")))
        .collect::<Vec<String>>()
}

/// Builds a message ID [Model] from a parser dictionary.
pub fn from_dictionary(dict: &parser::Dictionary, library_name: impl AsRef<str>) -> Result<Model> {
    let constants = dict
        .iter()
        .map(|(name, message)| Constant::new(name, gen_id(name, message), &split_message(message)))
        .collect::<Vec<Constant>>();
    Constant::validate(&constants)
        .with_context(|| "while validating the resulting intermediate FIDL file")?;
    let model = Model::new(library_name, &constants);
    Ok(model)
}

impl Constant {
    /// Creates a new Constant from supplied components.  The components are rendered verbatim.
    pub fn new(name: impl AsRef<str>, value: u64, comments: &[impl AsRef<str>]) -> Constant {
        // For now, we only uppercase.  We may do more if we need a wider variety
        // of identifiers.
        let constant_name = name.as_ref().to_owned().to_uppercase();
        Constant {
            name: constant_name,
            value,
            comments: comments.iter().map(|s| s.as_ref().to_owned()).collect(),
        }
    }

    // Validates the set of constants that were produced for FIDL generation. This is necessary
    // because not every combination of constant names that are valid in strings.xml are also
    // valid FIDL.  At the same time, this tries to not impose additional constraints onto
    // strings.xml, since most of the time the constraints mismatch between FIDL and strings.xml
    // don't matter.
    fn validate(constants: &[Constant]) -> Result<()> {
        let mut used_ids = HashSet::new();
        for constant in constants.iter() {
            let name: &str = &constant.name;
            if used_ids.contains(name) {
                return Err(anyhow::anyhow!(
                    "since we uppercase all identifiers, no two may share this FIDL name: {}, sorry",
                    name
                ));
            }
            used_ids.insert(name);
        }
        Ok(())
    }
}

/// The data model for a FIDL constants file.
///
/// Example:
///
/// ```
/// # use fidl::message_ids;
/// let data = message_ids::Fidl::new(
///   "library_name",
///   &vec![
///     message_ids::Constant::new("constant_name", 42 /* value */,
///       vec!["comment 1", "comment 2"],
///     ),
///   ],
/// );
#[derive(Serialize, Debug)]
pub struct Model {
    library: String,
    constants: Vec<Constant>,
}

impl Model {
    /// Creates a new Model from supplied components.
    pub fn new(library: impl AsRef<str>, constants: &[Constant]) -> Model {
        Model {
            library: library.as_ref().to_owned(),
            constants: constants.iter().map(|c| c.clone()).collect(),
        }
    }
}

static FIDL_FILE_TEMPLATE: &'static str = r#"// Generated by strings_to_fidl. DO NOT EDIT!

library {{library}};

type MessageIds = strict enum : uint64 {
  {{#each constants}}
  {{#each comments}}
    // {{{this}}}
  {{/each}}
    {{name}} = {{value}};
  {{/each}}
};
"#;

/// Renders the data model in [fidl] into a template, writing to [output].
pub fn render<W: io::Write>(fidl: Model, output: &mut W) -> Result<()> {
    let mut renderer = Handlebars::new();
    renderer
        .register_template_string("fidl", FIDL_FILE_TEMPLATE)
        .with_context(|| "while registering the file template")?;
    renderer
        .render_to_write("fidl", &fidl, output)
        .with_context(|| format!("while rendering content: {:?}", &fidl))?;
    Ok(())
}

#[cfg(test)]
mod tests {
    use {
        super::parser,
        super::*,
        anyhow::{Error, Result},
        xml::reader::EventReader,
    };

    #[test]
    fn render_template() -> Result<()> {
        let data = Model::new(
            "lib",
            &vec![Constant::new("constant_name", 42, &vec!["comment 1", "comment 2"])],
        );
        let mut output: Vec<u8> = vec![];
        render(data, &mut output).with_context(|| "test render_template")?;
        let result = String::from_utf8(output).with_context(|| "utf-8")?;
        assert_eq!(
            r#"// Generated by strings_to_fidl. DO NOT EDIT!

library lib;

type MessageIds = strict enum : uint64 {
    // comment 1
    // comment 2
    CONSTANT_NAME = 42;
};
"#,
            &result
        );
        Ok(())
    }

    #[test]
    fn xml_to_fidl() -> Result<(), Error> {
        struct TestCase {
            name: &'static str,
            content: &'static str,
            expected: &'static str,
        }
        let tests = vec![
            TestCase {
                name: "basic",
                content: r#"
               <!-- comment -->
               <?xml version="1.0" encoding="utf-8"?>
               <resources>
                 <!-- comment -->
                 <string
                   name="string_name"
                     >text_string</string>
               </resources>
            "#,
                expected: r#"// Generated by strings_to_fidl. DO NOT EDIT!

library library;

type MessageIds = strict enum : uint64 {
    // 'text_string'
    STRING_NAME = 17128972970596363087;
};
"#,
            },
            TestCase {
                name: "long string goes to the comments",
                content: r#"
               <!-- comment -->
               <?xml version="1.0" encoding="utf-8"?>
               <resources>
                 <!-- comment -->
                 <string
                   name="string_name"
                     >long long long long long long long long long long long long long</string>
               </resources>
            "#,
                expected: r#"// Generated by strings_to_fidl. DO NOT EDIT!

library library;

type MessageIds = strict enum : uint64 {
    // 'long long long long long long long long long long '
    // 'long long long'
    STRING_NAME = 18286701466023369706;
};
"#,
            },
            TestCase {
                name: "string with newline in comments",
                content: r#"
               <!-- comment -->
               <?xml version="1.0" encoding="utf-8"?>
               <resources>
                 <!-- comment -->
                 <string
                   name="string_name"
                     >text with
an intervening newline</string>
               </resources>
            "#,
                expected: r#"// Generated by strings_to_fidl. DO NOT EDIT!

library library;

type MessageIds = strict enum : uint64 {
    // 'text with an intervening newline'
    STRING_NAME = 18178872703820217636;
};
"#,
            },
            TestCase {
                // Not sure if we want to prevent this.
                name: "two identical strings with different IDs",
                content: r#"
               <!-- comment -->
               <?xml version="1.0" encoding="utf-8"?>
               <resources>
                 <!-- comment -->
                 <string
                   name="string_name"
                     >text</string>
                 <string
                   name="string_name_2"
                     >text</string>
               </resources>
            "#,
                expected: r#"// Generated by strings_to_fidl. DO NOT EDIT!

library library;

type MessageIds = strict enum : uint64 {
    // 'text'
    STRING_NAME = 15068421743305203572;
    // 'text'
    STRING_NAME_2 = 7479881158875375733;
};
"#,
            },
            TestCase {
                // Correct grapheme cluster split is *not* supported yet.
                name: "pangram in Serbian is split across lines correctly",
                content: r#"
               <!-- comment -->
               <?xml version="1.0" encoding="utf-8"?>
               <resources>
                 <!-- comment -->
                 <string
                   name="string_name"
                     >љубазни фењерџија чађавог лица хоће да ми покаже штос</string>
               </resources>
            "#,
                expected: r#"// Generated by strings_to_fidl. DO NOT EDIT!

library library;

type MessageIds = strict enum : uint64 {
    // 'љубазни фењерџија чађавог лица хоће да ми покаже ш'
    // 'тос'
    STRING_NAME = 3117970476734116355;
};
"#,
            },
            TestCase {
                // Correct grapheme cluster split is *not* supported yet.
                name: "pangram in Serbian is split across lines correctly",
                content: r#"
               <!-- comment -->
               <?xml version="1.0" encoding="utf-8"?>
               <resources>
                 <!-- comment -->
                 <string
                   name="string_name"
                     >љубазни фењерџија чађавог лица хоће да ми покаже штос</string>
               </resources>
            "#,
                expected: r#"// Generated by strings_to_fidl. DO NOT EDIT!

library library;

type MessageIds = strict enum : uint64 {
    // 'љубазни фењерџија чађавог лица хоће да ми покаже ш'
    // 'тос'
    STRING_NAME = 3117970476734116355;
};
"#,
            },
        ];
        for test in tests {
            let input = EventReader::from_str(&test.content);
            let mut parser = parser::Instance::new(false /* verbose */);
            let dict = parser.parse(input).with_context(|| format!("test: {}", &test.name))?;
            let mut output: Vec<u8> = vec![];
            let model = from_dictionary(dict, "library")
                .with_context(|| format!("test name: {}", &test.name))?;
            render(model, &mut output).with_context(|| format!("test name: {}", &test.name))?;
            let actual =
                String::from_utf8(output).with_context(|| format!("test name: {}", &test.name))?;
            assert_eq!(&test.expected, &actual);
        }
        Ok(())
    }

    #[test]
    fn xml_to_fidl_yields_error() -> Result<(), Error> {
        struct TestCase {
            name: &'static str,
            content: &'static str,
        }
        let tests = vec![TestCase {
            name: "conflicting uppercase is not allowed",
            content: r#"
               <!-- comment -->
               <?xml version="1.0" encoding="utf-8"?>
               <resources>
                 <!-- comment -->
                 <string
                   name="string_name"
                     >text_string</string>
                 <string
                   name="STRING_NAME"
                     >text_string_2</string>
               </resources>
            "#,
        }];
        #[allow(clippy::never_loop)] // TODO(fxbug.dev/95047)
        for test in tests {
            let input = EventReader::from_str(&test.content);
            let mut parser = parser::Instance::new(false /* verbose */);
            let dict = parser.parse(input).with_context(|| format!("test: {}", &test.name))?;
            match from_dictionary(dict, "library") {
                Err(_) => return Ok(()),
                Ok(_) => return Err(anyhow::anyhow!("unexpected OK for test: {}", test.name)),
            }
        }
        Ok(())
    }
}
