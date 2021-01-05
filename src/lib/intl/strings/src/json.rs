// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! # Building the JSON data model from parsed dictionaries.
//!
//! Use [model_from_dictionaries] to build a parsed data model for internationalized
//! messages.  This piece of code logically belongs to `intl_model`, but needed
//! to be factored out here, as it should only be compiled for the host toolchain.

use {
    crate::message_ids,
    crate::parser,
    anyhow::Result,
    intl_model::{Messages, Model},
    std::collections::BTreeMap,
};

#[cfg(test)]
pub fn as_messages(pairs: &Vec<(u64, String)>) -> Messages {
    let mut result = Messages::new();
    for (k, v) in pairs {
        result.insert(*k, v.clone());
    }
    result as Messages
}

/// Creates a new [Model] from the supplied dictionaries.
///
/// [source_locale_id] is the source locale (very typically would be "en-US" or some such);
/// [target_locale_id] is the target locale (i.e. the locale to translate into, e.g. "ru-RU");
/// source_dictionary is the dictionary of untranslated messages , presumably coming from the
/// declared source locale; and [target_dictionary] is the dictionary of available translated
/// messages, presumably in the target locale.
///
/// Both the source and target dictionaries are needed, because we need to determine whether
/// all messages are present.  At the moment we require that all messages present in the source
/// dictionary are also present in the target dictionary.  furthermore, messages that are
/// present in the target but not present in the source are omitted entirely.
pub fn model_from_dictionaries(
    source_locale_id: &str,
    source_dictionary: &parser::Dictionary,
    target_locale_id: &str,
    target_dictionary: &parser::Dictionary,
    replace_with_warning: bool,
) -> Result<Model> {
    let mut messages: Messages = BTreeMap::new();
    for (name, message) in source_dictionary.iter() {
        let message_id = message_ids::gen_id(name, message);
        match target_dictionary.get(name) {
            None => {
                if replace_with_warning {
                    messages.insert(message_id, format!("UNTRANSLATED({})", &message));
                } else {
                    return Err(anyhow::anyhow!(
                        "not found: translation for\n\tname: '{}'\n\tmessage: '{}'",
                        name,
                        message
                    ));
                }
            }
            Some(translated) => {
                messages.insert(message_id, translated.to_string());
            }
        }
    }

    let messages = messages;
    Ok(Model::from_parts(target_locale_id, source_locale_id, source_dictionary.len(), messages))
}

#[cfg(test)]
mod tests {
    use super::*;
    use anyhow::{Context, Result};
    use intl_model::Model;

    #[test]
    fn build_from_dictionary() -> Result<()> {
        struct TestCase {
            name: &'static str,
            // If set, a missing message is substituted.
            substitute: bool,
            source: parser::Dictionary,
            target: parser::Dictionary,
            expected_num_messages: usize,
            expected: Vec<(u64, String)>,
        }
        let tests = vec![
            TestCase {
                name: "basic pseudo-French",
                substitute: false,
                source: parser::Dictionary::from_init(&vec![("string_name", "text_string")]),
                target: parser::Dictionary::from_init(&vec![
                    // Behold my French-language-foo!
                    ("string_name", "un_string_textique"),
                ]),
                expected_num_messages: 1,
                // The magic numbers here and below are stable IDs generated for messages
                // by `message_ids::gen_id`.
                expected: vec![(17128972970596363087, "un_string_textique".to_string())],
            },
            TestCase {
                name: "known message ID",
                substitute: false,
                source: parser::Dictionary::from_init(&vec![("string_name", "text")]),
                target: parser::Dictionary::from_init(&vec![("string_name", "le text")]),
                expected_num_messages: 1,
                // The magic number here is a known golden result from the call to:
                // `message_ids::gen_id("string_name", "text")`.  There should not
                // be a change of the numeric message ID, unless `gen_id` algorithm
                // is also modified.
                expected: vec![(15068421743305203572, "le text".to_string())],
            },
            TestCase {
                name: "several messages",
                substitute: false,
                source: parser::Dictionary::from_init(&vec![
                    ("string_1", "text"),
                    ("string_2", "more text"),
                    ("string_3", "even more text"),
                ]),
                target: parser::Dictionary::from_init(&vec![
                    ("string_1", "le text"),
                    ("string_2", "le more text"),
                    ("string_3", "le even more text"),
                    ("string_4", "le text which has been abandoned"),
                ]),
                expected_num_messages: 3,
                expected: vec![
                    (2935634291568311942, "le text".to_string()),
                    (14010255246293253599, "le more text".to_string()),
                    (11619890714301104023, "le even more text".to_string()),
                ],
            },
            TestCase {
                name: "several messages",
                substitute: true,
                source: parser::Dictionary::from_init(&vec![
                    ("string_1", "text"),
                    ("string_2", "more text"),
                    ("string_3", "even more text"),
                ]),
                target: parser::Dictionary::from_init(&vec![
                    ("string_1", "le text"),
                    ("string_2", "le more text"),
                ]),
                expected_num_messages: 3,
                expected: vec![
                    (2935634291568311942, "le text".to_string()),
                    (14010255246293253599, "le more text".to_string()),
                    (11619890714301104023, "UNTRANSLATED(even more text)".to_string()),
                ],
            },
        ];
        for test in tests {
            let model = model_from_dictionaries(
                "und-x-src",
                &test.source,
                "und-x-dest",
                &test.target,
                test.substitute,
            )
            .with_context(|| format!("in test '{}', while building model", test.name))?;
            let expected = Model::from_parts(
                "und-x-dest",
                "und-x-src",
                test.expected_num_messages,
                super::as_messages(&test.expected),
            );
            assert_eq!(expected, model);
        }
        Ok(())
    }

    #[test]
    fn build_from_dictionary_fails() -> Result<()> {
        struct TestCase {
            name: &'static str,
            source: parser::Dictionary,
            target: parser::Dictionary,
        }
        let tests = vec![TestCase {
            name: "untranslated messages are disallowed",
            source: parser::Dictionary::from_init(&vec![
                ("string_1", "text"),
                ("string_2", "more text"),
                ("string_3", "even more text"),
                ("string_4", "untranslated text"),
            ]),
            target: parser::Dictionary::from_init(&vec![
                ("string_1", "le text"),
                ("string_2", "le more text"),
                ("string_3", "le even more text"),
            ]),
        }];
        for test in tests {
            let model = model_from_dictionaries(
                "und-x-src",
                &test.source,
                "und-x-dest",
                &test.target,
                false,
            );
            match model {
                Ok(_) => return Err(anyhow::anyhow!("unexpectedly passing test: '{}'", test.name)),
                Err(_) => { /* expected */ }
            }
        }
        Ok(())
    }
}
