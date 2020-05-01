// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! # A message ID generator for the `strings.xml` format
//!
//! This crate contains a binary `strings_to_fidl`, which generates message IDs from the
//! Android-formatted [strings.xml resource file][strings-xml], as a set of FIDL constants.
//! Complete support is not a specific goal, rather the generator will be amended to include more
//! features as more features are needed.
//!
//! [strings-xml]: https://developer.android.com/guide/topics/resources/string-resource

// TODO(fmil): temporary, until all code is used.
#![allow(dead_code)]

use {
    anyhow::anyhow,
    anyhow::Context,
    anyhow::Error,
    anyhow::Result,
    std::collections::BTreeMap,
    std::env,
    std::fs::File,
    std::io,
    std::path::PathBuf,
    structopt::StructOpt,
    xml::attribute,
    xml::reader::{EventReader, XmlEvent},
};

// TODO(fmil): Add usage link here.
#[derive(Debug, StructOpt)]
#[structopt(name = "Extracts information from strings.xml into FIDL")]
struct Args {
    #[structopt(long = "input", help = "The path to the input strings.xml format file")]
    input: PathBuf,
    #[structopt(long = "output", help = "The path to the output FIDL file")]
    output: PathBuf,
    #[structopt(long = "verbose", help = "Verbose output, for debugging")]
    verbose: bool,
    #[structopt(
        long = "library",
        default_value = "fuchsia.intl.l10n",
        help = "The FIDL library name for which to generate the code"
    )]
    library: String,
}

/// Like [eprintln!], but only if verbosity ($v) is true.
///
/// Example:
///
/// ```ignore
/// veprintln!(true, "this will be logged only if first param is true: {}", true);
/// ```
macro_rules! veprintln{
    ($v:expr, $($arg:tt)* ) => {{
        if $v {
            eprintln!($($arg)*);
        }
    }}
}

mod message_ids {

    use {
        crate::parser,
        anyhow::{Context, Result},
        handlebars::Handlebars,
        serde::Serialize,
        std::collections::hash_map::DefaultHasher,
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

    // Generates a locally unique identifier for [message].  The unique identifier
    // is generated based on the content, so any two messages identical in content
    // end up having the same identifier here.
    fn gen_id(message: impl AsRef<str>) -> u64 {
        let mut hasher = DefaultHasher::new();
        message.as_ref().hash(&mut hasher);
        hasher.finish()
    }

    // Splits the given internationalized messages into quoted strings of fixed size.  Used to give
    // reasonably formatted usage hint in the generated code.
    fn split_message(message: impl AsRef<str>) -> Vec<String> {
        const SLICE: usize = 50;
        // This is not iterating over grapheme clusters, but for now will have
        // to do.
        message.as_ref()
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
    pub fn from_dictionary(
        dict: &parser::Dictionary,
        library_name: impl AsRef<str>,
    ) -> Result<Model> {
        let constants = dict
            .iter()
            .map(|(name, message)| Constant::new(name, gen_id(message), &split_message(message)))
            .collect::<Vec<Constant>>();
        let model = Model::new(library_name, &constants);
        Ok(model)
    }

    impl Constant {
        /// Creates a new Constant from supplied components.  The components are rendered verbatim.
        pub fn new(name: impl AsRef<str>, value: u64, comments: &[impl AsRef<str>]) -> Constant {
            Constant {
                name: name.as_ref().to_owned(),
                value,
                comments: comments.iter().map(|s| s.as_ref().to_owned()).collect(),
            }
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

enum MessageIds : uint64 {
  {{~#each constants~}}
  {{~#each comments}}
    // {{this}}
  {{~/each}}
    {{name}} = {{value}};
  {{~/each}}
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
}

/// The XML parser that turns `strings.xml` into a [Dictionary].
mod parser {

    use super::*;

    /// A dictionary obtained from calling [Instance::parse].
    #[derive(Eq, PartialEq, Debug, Clone)]
    pub struct Dictionary {
        messages: BTreeMap<String, String>,
    }

    mod dict {
        use std::collections::btree_map;
        use std::iter;
        /// An iterator for [Dictionary], use Dictionary::iter(), as usual.
        pub struct Iter<'a> {
            pub(crate) rep: btree_map::Iter<'a, String, String>,
        }
        impl<'a> iter::Iterator for Iter<'a> {
            type Item = (&'a String, &'a String);
            fn next(&mut self) -> Option<<Self as iter::Iterator>::Item> {
                self.rep.next()
            }
        }
    }

    impl Dictionary {
        /// Make a new instance of Dictionary.
        pub fn new() -> Dictionary {
            Dictionary { messages: BTreeMap::new() }
        }

        /// Obtains an iterator over dictionary key-value pairs.
        pub fn iter(&self) -> dict::Iter<'_> {
            dict::Iter { rep: self.messages.iter() }
        }

        /// Insert a mapping `{ key => value }`.
        pub fn insert(&mut self, key: String, value: String) {
            self.messages.insert(key, value);
        }

        /// Returns true if `key` exists in the dictionary.
        pub fn contains_key(&self, key: &str) -> bool {
            self.messages.contains_key(key)
        }

        /// Creates a [Dictionary] from a sequence of `{ key => value}` pairs.
        pub(crate) fn from_init(init: &[(&str, &str)]) -> Dictionary {
            let mut result = Self::new();
            for (k, v) in init {
                result.insert(k.to_string(), v.to_string());
            }
            result
        }
    }

    /// The parser states.
    ///
    /// Currently, [State] does not implement state transitions, since I have not found how to do
    /// that, for self-transitions (transition from a state, to the same state).  Instead,
    /// [parser::Instance] deals with state transitions, and has a function named `init` that
    /// handles state transition for [State::Init] and so on, for all states.
    #[derive(Debug, Eq, PartialEq)]
    enum State {
        // Document has not started.
        Init,

        // Document start has been seen.
        StartDocument,

        // Resources section started, but no specifics.
        Resources,

        // Parsing the string section. [name] is the name of the string id, and
        // [text] is the accumulated text so far.
        String { name: String, text: String },

        // Parsing the xliff:g section,
        XliffG { name: String, text: String },

        // Final accepting state of the parser, no more input is expected.
        Done,
    }

    /// The XML parser state.
    #[derive(Debug)]
    pub struct Instance {
        // The built out dictionary.
        messages: Dictionary,

        // The current parser state.
        state: State,

        // If set, the parser prints internal state at key points in the parsing
        // to stderr.
        verbose: bool,
    }

    /// A parser for `strings.xml`.  The implementation does not strive for completeness, but
    /// should be easy to amend if we find something missing.  The implementation tests contain
    /// golden examples of passing and failing documents.
    impl Instance {
        /// Creates a new empty Parser instance
        pub fn new(verbose: bool) -> Instance {
            Instance { messages: Dictionary::new(), state: State::Init, verbose }
        }

        // State::Init transitions on e.
        fn init(&mut self, e: XmlEvent) -> Result<()> {
            match e {
                XmlEvent::StartDocument { .. } => {
                    self.state = State::StartDocument;
                }
                XmlEvent::EndDocument => {
                    self.state = State::Done;
                }
                XmlEvent::Whitespace { .. } => { /* ignore whitespace */ }
                _ => {
                    return Err(anyhow!("unimplemented state: {:?}, token: {:?}", &self.state, &e))
                }
            }
            Ok(())
        }

        // State::StartDocument transitions on e.
        fn start_document(&mut self, e: XmlEvent) -> Result<()> {
            match e {
                XmlEvent::StartElement { ref name, .. } => {
                    if name.local_name != "resources" {
                        return Err(anyhow!("expected StartElement(resources), got: {:?}", &e));
                    }
                    self.state = State::Resources;
                }
                _ => {
                    return Err(anyhow!("unimplemented state: {:?}, token: {:?}", &self.state, &e))
                }
            }
            Ok(())
        }

        /// Finds attribute named `name` in the sequence of owned attributes.  For the time being
        /// the comparison is simplistic and does not consider XML namespacing.
        fn find_attribute(name: &str, attributes: &[attribute::OwnedAttribute]) -> Result<String> {
            for ref attr in attributes {
                let attr_name = &attr.name;
                if attr_name.local_name != name {
                    continue;
                }
                return Ok(attr.value.to_owned());
            }
            Err(anyhow!("attribute not found: {}", name))
        }

        // State::Resources transitions on e.
        fn resources(&mut self, e: XmlEvent) -> Result<()> {
            match e {
                XmlEvent::StartElement { ref name, ref attributes, .. } => {
                    if name.local_name == "string" {
                        self.state = State::String {
                            name: Instance::find_attribute("name", &attributes[..]).with_context(
                                || "while looking for attribute 'name' in 'string'",
                            )?,
                            text: "".to_string(),
                        };
                    } else {
                        return Err(anyhow!("expected StartElement(string), got: {:?}", &e));
                    }
                }
                XmlEvent::EndElement { ref name, .. } => {
                    if name.local_name != "resources" {
                        return Err(anyhow!("expected EndElement(resources), got: {:?}", &e));
                    }
                    self.state = State::Init;
                }
                XmlEvent::Whitespace { .. } => { /* ignore whitespace */ }
                _ => return Err(anyhow!("expected StartElement(string), got: {:?}", &e)),
            }
            Ok(())
        }

        // Implements state transitions for [State::String].  `token_name` is the message
        // ID being processed, and `text` is the currently accummulated string (since the string
        // may be broken off across multiple <xliff:g> tags.
        fn string(&mut self, token_name: String, text: String, e: XmlEvent) -> Result<()> {
            match e {
                XmlEvent::StartElement { ref name, .. } => {
                    // Needs a better way to compare namespace and element.
                    let ns = name.namespace.clone();
                    if name.local_name != "g" && ns.unwrap_or("".to_string()) != "xliff" {
                        return Err(anyhow!("unexpected StartElement({:?})", &e));
                    }
                    // xliff:g is starting
                    self.state = State::XliffG { name: token_name, text };
                }
                XmlEvent::Characters(chars) => {
                    // There are probably more ways in which we can get characters, like cdata
                    // and such.
                    self.state = State::String { name: token_name, text: text.to_owned() + &chars };
                }
                XmlEvent::EndElement { ref name, .. } => {
                    if name.local_name != "string" {
                        return Err(anyhow!("expected EndElement(string), got: {:?}", &e));
                    }
                    if self.messages.contains_key(&token_name) {
                        return Err(anyhow!("duplicate string with name: {}", token_name));
                    }
                    self.messages.insert(token_name, text);
                    self.state = State::Resources;
                }
                XmlEvent::Whitespace { .. } => { /* ignore whitespace */ }
                _ => {
                    return Err(anyhow!("unimplemented state: {:?}, token: {:?}", &self.state, &e))
                }
            }
            Ok(())
        }

        // Implements state transitions for [State::XliffG].  Parameters are the same as in
        // the function [Instance::string] above.
        fn xliffg(&mut self, token_name: String, text: String, e: XmlEvent) -> Result<()> {
            match e {
                XmlEvent::StartElement { ref name, .. } => {
                    return Err(anyhow!("unexpected StartElement({:?})", &name));
                }
                XmlEvent::Characters(chars) => {
                    // There are probably more ways in which we can get characters, like cdata
                    // and such.
                    self.state = State::XliffG { name: token_name, text: text.to_owned() + &chars };
                }
                XmlEvent::EndElement { ref name, .. } => {
                    let ns = name.namespace.clone();
                    if name.local_name != "g" && ns.unwrap_or("".to_string()) != "xliff" {
                        return Err(anyhow!("expected EndElement(xliff:g), got: {:?}", &e));
                    }
                    self.state = State::String { name: token_name, text };
                }
                XmlEvent::Whitespace { .. } => { /* ignore whitespace */ }
                _ => {
                    return Err(anyhow!("unimplemented state: {:?}, token: {:?}", &self.state, &e))
                }
            }
            Ok(())
        }

        // Nudges a state machine across transitions.  The states are given by the [State] enum,
        // all states observing the sequence of [XmlEvent]s.
        fn consume(&mut self, e: XmlEvent) -> Result<()> {
            veprintln!(self.verbose, "parser.parse: event {:?},\n\tstate: {:?}", &e, &self.state);
            match &self.state {
                State::Init => {
                    self.init(e)?;
                }
                State::StartDocument => {
                    self.start_document(e)?;
                }
                State::Resources => {
                    self.resources(e)?;
                }
                State::String { ref name, ref text } => {
                    let name = name.to_owned();
                    let text = text.to_owned();
                    self.string(name, text, e)?;
                }
                State::XliffG { ref name, ref text } => {
                    let name = name.to_owned();
                    let text = text.to_owned();
                    self.xliffg(name, text, e)?;
                }
                State::Done => {
                    return Err(anyhow!(
                    "document has been fully parsed, no other parse events expected, but got: {:?}",
                    e
                ))
                }
            }
            Ok(())
        }

        /// Parses the document that the [reader] is configured to yield.
        pub fn parse<R>(&mut self, reader: EventReader<R>) -> Result<&Dictionary>
        where
            R: io::Read,
        {
            veprintln!(self.verbose, "parser.parse: ENTER");
            for event in reader {
                match event {
                    Ok(e) => {
                        // Sadly it is not possible to report the position at
                        // which the error happened, as reader is consumed by
                        // the loop together with its position-reporting.
                        self.consume(e).with_context(|| format!("in state: {:?}", self.state))?;
                    }
                    Err(e) => return Err(Error::new(e)),
                }
            }
            veprintln!(self.verbose, "parser.parse: LEAVE");
            Ok(&self.messages)
        }
    }
}

/// Open the needed files, and handle usual errors.
fn open_files(args: &Args) -> Result<(impl io::Read, impl io::Write), Error> {
    let input_str = args.input.to_str().with_context(|| {
        format!("input filename is not utf-8, what? Use --verbose flag to print the value.")
    })?;
    let input = io::BufReader::new(
        File::open(&args.input)
            .with_context(|| format!("could not open input file: {}", input_str))?,
    );
    let output_str = args.output.to_str().with_context(|| {
        format!("output filename is not utf-8, what? Use --verbose flag to print the value.")
    })?;
    let output = File::create(&args.output)
        .with_context(|| format!("could not open output file: {}", output_str))?;
    Ok((input, output))
}

fn main() -> Result<(), Error> {
    env::set_var("RUST_BACKTRACE", "full");
    let args: Args = Args::from_args();
    veprintln!(args.verbose, "args: {:?}", args);

    let (input, mut output) = open_files(&args).with_context(|| "while opening files")?;
    let reader = EventReader::new(input);

    let mut parser = parser::Instance::new(args.verbose);
    let dictionary = parser.parse(reader).with_context(|| "while parsing dictionary")?;
    let model = message_ids::from_dictionary(dictionary, &args.library)?;
    message_ids::render(model, &mut output)?;
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::parser::*;
    use super::*;

    #[test]
    fn read_ok() -> Result<(), Error> {
        struct TestCase {
            name: &'static str,
            content: &'static str,
            expected: Dictionary,
        };
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
                expected: Dictionary::from_init(&vec![("string_name", "text_string")]),
            },
            TestCase {
                name: "two_entries",
                content: r#"
               <!-- comment -->
               <?xml version="1.0" encoding="utf-8"?>
               <resources>
                 <!-- comment -->
                 <string
                   name="string_name"
                     >text_string</string>
                 <string
                   name="string_name_2"
                     >
this is a second string
with intervening newlines
</string>
               </resources>
            "#,
                expected: Dictionary::from_init(&vec![
                    ("string_name", "text_string"),
                    ("string_name_2", "\nthis is a second string\nwith intervening newlines\n"),
                ]),
            },
            TestCase {
                name: "parse xliff:g",
                content: r#"
               <!-- comment -->
               <?xml version="1.0" encoding="utf-8"?>
               <resources
                 xmlns:xliff="urn:oasis:names:tc:xliff:document:1.2">
                 <string name="countdown">
                   <xliff:g id="time" example="5 days">{time}</xliff:g> until holiday</string>
               </resources>
            "#,
                expected: Dictionary::from_init(&vec![("countdown", "{time} until holiday")]),
            },
            TestCase {
                name: "parse xliff:g whitespace",
                content: r#"
               <!-- comment -->
               <?xml version="1.0" encoding="utf-8"?>
               <resources
                 xmlns:xliff="urn:oasis:names:tc:xliff:document:1.2">
                 <string name="countdown">
                   <xliff:g id="time" example="5 days">{time}</xliff:g> until holiday
</string>
               </resources>
            "#,
                expected: Dictionary::from_init(&vec![("countdown", "{time} until holiday\n")]),
            },
        ];
        for test in tests {
            let input = EventReader::from_str(&test.content);
            let mut parser = parser::Instance::new(true /* verbose */);
            let actual =
                parser.parse(input).with_context(|| format!("for test item: {}", test.name))?;
            assert_eq!(&test.expected, actual);
        }
        Ok(())
    }

    #[test]
    fn read_error() -> Result<(), Error> {
        struct TestCase {
            name: &'static str,
            content: &'static str,
        };
        let tests = vec![
            TestCase {
                name: "duplicate_elements",
                content: r#"
               <!-- comment -->
               <?xml version="1.0" encoding="utf-8"?>
               <resources>
                 <!-- comment -->
                 <string
                   name="string_name"
                     >text_string</string>
                 <string
                   name="string_name"
                     >text_string_2</string>
               </resources>
            "#,
            },
            TestCase {
                name: "duplicate resources section",
                content: r#"
               <!-- comment -->
               <?xml version="1.0" encoding="utf-8"?>
               <resources>
                 <!-- comment -->
                 <string
                   name="string_name"
                     >text_string</string>
                 <string
                   name="string_name"
                     >text_string_2</string>
               </resources>
               <resources>
               </resources>
            "#,
            },
        ];
        for test in tests {
            let input = EventReader::from_str(&test.content);
            let mut parser = parser::Instance::new(false /* verbose */);
            let actual = parser.parse(input);
            match actual {
                Ok(_) => {
                    return Err(anyhow!("unexpected OK in test: {}", test.name));
                }
                _ => {}
            }
        }
        Ok(())
    }

    #[test]
    fn render_template() -> Result<()> {
        let data = message_ids::Model::new(
            "lib",
            &vec![message_ids::Constant::new("constant_name", 42, &vec!["comment 1", "comment 2"])],
        );
        let mut output: Vec<u8> = vec![];
        message_ids::render(data, &mut output).with_context(|| "test render_template")?;
        let result = String::from_utf8(output).with_context(|| "utf-8")?;
        assert_eq!(
            r#"// Generated by strings_to_fidl. DO NOT EDIT!

library lib;

enum MessageIds : uint64 {
    // comment 1
    // comment 2
    constant_name = 42;
};
"#,
            &result
        );
        Ok(())
    }

    #[test]
    fn xml_to_fidl() -> Result<(), Error> {
        use crate::message_ids;
        struct TestCase {
            name: &'static str,
            content: &'static str,
            expected: &'static str,
        };
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

enum MessageIds : uint64 {
    // 'text_string'
    string_name = 16479498444549137679;
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

enum MessageIds : uint64 {
    // 'long long long long long long long long long long '
    // 'long long long'
    string_name = 18059451882864842113;
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

enum MessageIds : uint64 {
    // 'text with an intervening newline'
    string_name = 7254939001732941199;
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

enum MessageIds : uint64 {
    // 'text'
    string_name = 9737151508951142383;
    // 'text'
    string_name_2 = 9737151508951142383;
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

enum MessageIds : uint64 {
    // 'љубазни фењерџија чађавог лица хоће да ми покаже ш'
    // 'тос'
    string_name = 10417041116524418271;
};
"#,
            },
        ];
        for test in tests {
            let input = EventReader::from_str(&test.content);
            let mut parser = parser::Instance::new(false /* verbose */);
            let dict = parser.parse(input).with_context(|| format!("test: {}", &test.name))?;
            let mut output: Vec<u8> = vec![];
            let model = message_ids::from_dictionary(dict, "library")
                .with_context(|| format!("test name: {}", &test.name))?;
            message_ids::render(model, &mut output)
                .with_context(|| format!("test name: {}", &test.name))?;
            let actual =
                String::from_utf8(output).with_context(|| format!("test name: {}", &test.name))?;
            assert_eq!(&test.expected, &actual);
        }
        Ok(())
    }
}
