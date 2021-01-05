// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! The XML parser that turns `strings.xml` into a [Dictionary].

use {
    anyhow::{anyhow, Context, Error, Result},
    lazy_static::lazy_static,
    regex::Regex,
    std::collections::BTreeMap,
    std::io,
    xml::attribute,
    xml::reader::{EventReader, ParserConfig, XmlEvent},
};

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

    /// Looks up a message by [key].
    pub fn get(&self, key: &str) -> Option<&str> {
        self.messages.get(key).map(|k| k.as_str())
    }

    /// Returns the number of elements in the dictionary.
    pub fn len(&self) -> usize {
        self.messages.len()
    }

    /// Creates a [Dictionary] from a sequence of `{ key => value}` pairs.
    #[cfg(test)]
    pub fn from_init(init: &[(&str, &str)]) -> Dictionary {
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

lazy_static! {
    // All string names must match this regex string. (This should be fixed up
    // to match the FIDL naming rules)
    //
    // Example:
    // __hello_WORLD -- acceptable
    // 0cool -- not acceptable, leading number
    // добар_дан -- not acceptable, not A-Z.
    static ref RAW_REGEX_STRING: &'static str = r"^[_a-zA-Z][_a-zA-Z_0-9]*$";

    static ref VALID_TOKEN_NAME: Regex = Regex::new(&RAW_REGEX_STRING).unwrap();
}

// Verifies that [name] is a valid string name.
pub(crate) fn validate_token_name(name: &str, verbose: bool) -> bool {
    let is_match = VALID_TOKEN_NAME.is_match(name);
    veprintln!(
        verbose,
        "validate_token_name: checking identifier: '{}'; is_match: {}",
        name,
        is_match
    );
    is_match
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

    // Configures the reader correctly for XML parsing.
    pub fn reader<R: std::io::Read>(source: R) -> EventReader<R> {
        EventReader::new_with_config(source, ParserConfig::new().ignore_comments(false))
    }

    // State::Init transitions on e.
    fn init(&mut self, e: XmlEvent) -> Result<()> {
        match e {
            XmlEvent::Comment { .. } => {
                return Err(anyhow!("comments are not allowed before <?xml ... ?>"));
            }
            XmlEvent::StartDocument { .. } => {
                self.state = State::StartDocument;
            }
            XmlEvent::EndDocument => {
                self.state = State::Done;
            }
            XmlEvent::Whitespace { .. } => { /* ignore whitespace */ }
            _ => return Err(anyhow!("unimplemented state: {:?}, token: {:?}", &self.state, &e)),
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
            XmlEvent::Comment { .. } => {}
            _ => return Err(anyhow!("unimplemented state: {:?}, token: {:?}", &self.state, &e)),
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
                        name: Instance::find_attribute("name", &attributes[..])
                            .with_context(|| "while looking for attribute 'name' in 'string'")?,
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
            XmlEvent::Comment { .. } => {}
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
                if !validate_token_name(&token_name, self.verbose) {
                    return Err(anyhow!(
                        "name is not acceptable: '{}', does not match: {}",
                        &token_name,
                        *RAW_REGEX_STRING
                    ));
                }
                self.messages.insert(token_name, text);
                self.state = State::Resources;
            }
            XmlEvent::Whitespace { .. } => { /* ignore whitespace */ }
            XmlEvent::Comment { .. } => {}
            _ => return Err(anyhow!("unimplemented state: {:?}, token: {:?}", &self.state, &e)),
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
            XmlEvent::Comment { .. } => {}
            _ => return Err(anyhow!("unimplemented state: {:?}, token: {:?}", &self.state, &e)),
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

#[cfg(test)]
mod tests {
    use {
        super::{Dictionary, Instance},
        anyhow::{anyhow, Context, Error, Result},
    };

    #[test]
    fn dictionary_api() -> Result<(), Error> {
        let d = Dictionary::from_init(&vec![("string_name", "text_string")]);
        assert_eq!(d.len(), 1);
        assert_eq!(d.get("string_name").unwrap(), "text_string");
        assert!(d.get("unknown").is_none(), "text_string");
        assert_eq!(d.iter().map(|(k, _)| k.as_str()).collect::<Vec<&str>>(), vec!["string_name"]);
        assert_eq!(d.iter().map(|(_, v)| v.as_str()).collect::<Vec<&str>>(), vec!["text_string"]);
        Ok(())
    }

    #[test]
    fn read_ok() -> Result<(), Error> {
        struct TestCase {
            name: &'static str,
            content: &'static str,
            expected: Dictionary,
        }
        let tests = vec![
            TestCase {
                name: "basic",
                content: r#"
               <?xml version="1.0" encoding="utf-8"?>
               <!-- comment -->
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
               <?xml version="1.0" encoding="utf-8"?>
               <resources>
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
            TestCase {
                name: "comments everywhere",
                content: r#"
               <?xml version="1.0" encoding="utf-8"?>
               <!-- comment -->
               <resources
                 xmlns:xliff="urn:oasis:names:tc:xliff:document:1.2">
                 <!-- comment -->
                 <string name="countdown">
                   <!-- comment -->
                   <xliff:g id="time" example="5 days"
                     ><!-- comment -->{time}</xliff:g> until holiday
</string>
               </resources>
            "#,
                expected: Dictionary::from_init(&vec![("countdown", "{time} until holiday\n")]),
            },
        ];
        for test in tests {
            let input = Instance::reader(test.content.as_bytes());
            let mut parser = Instance::new(true /* verbose */);
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
        }
        let tests = vec![
            TestCase {
                name: "duplicate_elements",
                content: r#"
               <?xml version="1.0" encoding="utf-8"?>
               <resources>
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
               <?xml version="1.0" encoding="utf-8"?>
               <resources>
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
            TestCase {
                name: "string_name has unexpected comma",
                content: r#"
               <?xml version="1.0" encoding="utf-8"?>
               <resources>
                 <string
                   name="string_name,"
                     >text_string</string>
               </resources>
            "#,
            },
            TestCase {
                name: "comment before ?xml is not allowed",
                content: r#"
               <!-- comment -->
               <?xml version="1.0" encoding="utf-8"?>
               <resources>
                 <string
                   name="string_name"
                     >text_string</string>
               </resources>
            "#,
            },
        ];
        for test in tests {
            let input = Instance::reader(test.content.as_bytes());
            let mut parser = Instance::new(false /* verbose */);
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

    use super::validate_token_name;

    #[test]
    fn acceptable_and_unacceptable_names() -> Result<(), Error> {
        assert_eq!(true, validate_token_name("_hello", true /*verbose*/));
        assert_eq!(true, validate_token_name("__hello", true /*verbose*/));
        assert_eq!(true, validate_token_name("__HELLO__", true /*verbose*/));
        assert_eq!(true, validate_token_name("__H_e_l_l_o__", true /*verbose*/));
        assert_eq!(true, validate_token_name("__H_e_l_l_o_1234__", true /*verbose*/));

        // Starts with zero
        assert_eq!(false, validate_token_name("0cool", true /*verbose*/));
        // Unexpected comma.
        assert_eq!(false, validate_token_name("role_table,", true /*verbose*/));
        // Unicode overdose.
        assert_eq!(false, validate_token_name("хелло_њорлд", true /*verbose*/));
        Ok(())
    }
}
