// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Defines the lexer for the Documentation Compiler.
//! It extracts all the lexical items (with their location) from a source text (Documentation).

use crate::source::Location;
use crate::source::Source;
use crate::DocCompiler;
use std::rc::Rc;
use std::str::CharIndices;

/// Defines all the lexical items we can parse.
pub enum LexicalContent {
    /// A number. For example:
    /// - 1234
    /// - 0xabcd
    Number(String),

    /// A name from an English text point of view. For example:
    /// - Hello
    /// - heap-specific
    /// - doesn't
    Name(String),

    /// A reference to an existing applicative concept like a method name, a field name, ...
    /// A reference is held between back quotes.
    Reference(String),

    /// The end of an english sentence.
    /// For example ".", ":", "!", "?".
    EndOfSentence(char),

    /// Some consecutive blank spaces.
    Spaces(u32),

    /// Some consecutive new lines.
    NewLines(u32),

    /// The end of the documentation. The parser generates one and only one EndOfInput.
    /// This is always the last item in the list.
    EndOfInput,
}

/// Defines a lexical item (item + location in the source file).
pub struct LexicalItem {
    pub location: Location,
    pub content: LexicalContent,
}

/// Parses some documentation and extract all the lexical items found in the documentation.
///
/// Returns the reduced items.
pub fn reduce_lexems(compiler: &mut DocCompiler, source: &Rc<Source>) -> Option<Vec<LexicalItem>> {
    let mut items: Vec<LexicalItem> = Vec::new();
    let mut ok = true;
    let mut iter = source.text.char_indices();
    let mut current = iter.next();
    while let Some((index, character)) = current {
        current = match character {
            '0'..='9' => reduce_number_or_name(&mut items, &source, index, character, &mut iter),
            'a'..='z' | 'A'..='Z' | '_' => reduce_name(&mut items, &source, index, &mut iter),
            '`' => reduce_reference(compiler, &mut items, &source, &mut iter),
            '.' | ':' | '!' | '?' => {
                items.push(LexicalItem {
                    location: Location { source: Rc::clone(&source), start: index, end: index },
                    content: LexicalContent::EndOfSentence(character),
                });
                iter.next()
            }
            ' ' => reduce_spaces(&mut items, &source, index, &mut iter),
            '\n' => reduce_new_lines(&mut items, &source, index, &mut iter),
            // Unknown character.
            _ => {
                ok = false;
                compiler.add_error(
                    &Location { source: Rc::clone(&source), start: index, end: index },
                    format!("Unknown character <{}>", character),
                );
                iter.next()
            }
        }
    }
    // We reached the end of the text. We add a EndOfInput item. This way, the next stage won't
    // need to check if we are at the end of the vector when trying to reduce something.
    items.push(LexicalItem {
        location: Location {
            source: Rc::clone(&source),
            start: source.text.len(),
            end: source.text.len(),
        },
        content: LexicalContent::EndOfInput,
    });
    if ok {
        Some(items)
    } else {
        None
    }
}

/// Reduces a number.
///
/// For example:
/// - 1234
/// - 0xabcd
///
/// If we find characters which are not valid for a number but valid for a name then, the whole
/// sequence of characters (including the leading digits) are used to return a Name.
///
/// For example:
/// - 123abc
fn reduce_number_or_name(
    items: &mut Vec<LexicalItem>,
    source: &Rc<Source>,
    start: usize,
    first_character: char,
    iter: &mut CharIndices<'_>,
) -> Option<(usize, char)> {
    let mut hexadecimal = false;
    let mut number = true;
    let mut current: Option<(usize, char)> = iter.next();
    if first_character == '0' {
        // Checks for an hexadecimal number.
        if let Some((_, character)) = current {
            if character == 'x' || character == 'X' {
                hexadecimal = true;
                current = iter.next();
            }
        }
    }
    let end = loop {
        match current {
            Some((index, character)) => match character {
                '0'..='9' => {}
                'a'..='f' | 'A'..='F' => {
                    if !hexadecimal {
                        number = false;
                    }
                }
                'g'..='z' | 'G'..='Z' | '_' | '-' | '\'' => number = false,
                _ => {
                    break index;
                }
            },
            None => {
                break source.text.len();
            }
        }
        current = iter.next();
    };
    if number {
        // Only digits (potentially hexadecimal) have been found => Number.
        items.push(LexicalItem {
            location: Location { source: Rc::clone(&source), start, end },
            content: LexicalContent::Number(source.text[start..end].to_string()),
        });
    } else {
        // Not a valid number => Name.
        items.push(LexicalItem {
            location: Location { source: Rc::clone(&source), start, end },
            content: LexicalContent::Name(source.text[start..end].to_string()),
        });
    }
    current
}

/// Reduces a name.
///
/// A name is a valid name from the English language point of view.
///
/// Examples of valid names:
/// - Hello
/// - heap-specific
/// - doesn't
fn reduce_name(
    items: &mut Vec<LexicalItem>,
    source: &Rc<Source>,
    start: usize,
    iter: &mut CharIndices<'_>,
) -> Option<(usize, char)> {
    let mut current: Option<(usize, char)>;
    let end = loop {
        current = iter.next();
        match current {
            Some((index, character)) => match character {
                'a'..='z' | 'A'..='Z' | '0'..='9' | '_' | '-' | '\'' => {}
                _ => {
                    break index;
                }
            },
            None => {
                break source.text.len();
            }
        }
    };
    items.push(LexicalItem {
        location: Location { source: Rc::clone(&source), start, end },
        content: LexicalContent::Name(source.text[start..end].to_string()),
    });
    current
}

/// Reduces a reference (text between back quotes).
fn reduce_reference(
    compiler: &mut DocCompiler,
    items: &mut Vec<LexicalItem>,
    source: &Rc<Source>,
    iter: &mut CharIndices<'_>,
) -> Option<(usize, char)> {
    let mut current = iter.next();
    let start = if let Some((index, _)) = current { index } else { 0 };
    loop {
        match current {
            Some((index, character)) => match character {
                '`' => {
                    items.push(LexicalItem {
                        location: Location { source: Rc::clone(&source), start, end: index },
                        content: LexicalContent::Reference(source.text[start..index].to_string()),
                    });
                    current = iter.next();
                    break;
                }
                _ => {}
            },
            None => {
                compiler.add_error(
                    &Location { source: Rc::clone(&source), start, end: source.text.len() },
                    "Unterminated reference".to_owned(),
                );
                break;
            }
        }
        current = iter.next();
    }
    current
}

/// Reduces spaces (at least one).
fn reduce_spaces(
    items: &mut Vec<LexicalItem>,
    source: &Rc<Source>,
    start: usize,
    iter: &mut CharIndices<'_>,
) -> Option<(usize, char)> {
    // Counts the number of blank spaces found.
    let mut count = 1;
    let mut current: Option<(usize, char)>;
    let end = loop {
        current = iter.next();
        match current {
            Some((index, character)) => match character {
                ' ' => count += 1,
                _ => {
                    break index;
                }
            },
            None => {
                break source.text.len();
            }
        }
    };
    items.push(LexicalItem {
        location: Location { source: Rc::clone(&source), start, end },
        content: LexicalContent::Spaces(count),
    });
    current
}

/// Reduces new lines (at least one).
///
/// Consecutives new lines are grouped together.
/// A count greater than 1 means one new line followed by (count - 1) blank lines.
fn reduce_new_lines(
    items: &mut Vec<LexicalItem>,
    source: &Rc<Source>,
    start: usize,
    iter: &mut CharIndices<'_>,
) -> Option<(usize, char)> {
    // Counts the number of new lines found.
    let mut count = 1;
    let mut current: Option<(usize, char)>;
    let end = loop {
        current = iter.next();
        match current {
            Some((index, character)) => match character {
                '\n' => count += 1,
                _ => {
                    break index;
                }
            },
            None => {
                break source.text.len();
            }
        }
    };
    items.push(LexicalItem {
        location: Location { source: Rc::clone(&source), start, end },
        content: LexicalContent::NewLines(count),
    });
    current
}

#[cfg(test)]
mod test {
    use crate::lexer::reduce_lexems;
    use crate::lexer::LexicalContent;
    use crate::lexer::LexicalItem;
    use crate::source::Source;
    use crate::DocCompiler;
    use std::rc::Rc;

    /// Convert the lexical items to text to be able to check them.
    fn lexical_items_to_errors(
        compiler: &mut DocCompiler,
        items: &Vec<LexicalItem>,
        with_spaces: bool,
    ) {
        for item in items.iter() {
            match &item.content {
                LexicalContent::Number(text) => {
                    compiler.add_error(&item.location, format!("Number <{}>", text))
                }
                LexicalContent::Name(text) => {
                    compiler.add_error(&item.location, format!("Name <{}>", text))
                }
                LexicalContent::Reference(text) => {
                    compiler.add_error(&item.location, format!("Reference <{}>", text))
                }
                LexicalContent::EndOfSentence(character) => {
                    compiler.add_error(&item.location, format!("EndOfSentence <{}>", character))
                }
                LexicalContent::Spaces(count) => {
                    if with_spaces {
                        compiler.add_error(&item.location, format!("Spaces ({})", count));
                    }
                }
                LexicalContent::NewLines(count) => {
                    if with_spaces {
                        compiler.add_error(&item.location, format!("NewLines ({})", count));
                    }
                }
                LexicalContent::EndOfInput => {
                    if with_spaces {
                        compiler.add_error(&item.location, "End".to_owned());
                    }
                }
            }
        }
    }

    #[test]
    fn lexer_ok() {
        let mut compiler = DocCompiler::new();
        let source = Rc::new(Source::new(
            "sdk/foo/foo.fidl".to_owned(),
            10,
            4,
            "Some documentation.\n".to_owned(),
        ));
        let items = reduce_lexems(&mut compiler, &source);
        assert!(!items.is_none());
        assert!(compiler.errors.is_empty());
    }

    #[test]
    fn lexer_numbers() {
        let mut compiler = DocCompiler::new();
        let source = Rc::new(Source::new(
            "sdk/foo/foo.fidl".to_owned(),
            10,
            4,
            "1234 0x789abc 123abc 0x78abg".to_owned(),
        ));
        let items = reduce_lexems(&mut compiler, &source);
        assert!(!items.is_none());
        lexical_items_to_errors(&mut compiler, &items.unwrap(), /*with_spaces=*/ false);
        assert_eq!(
            compiler.errors,
            "\
1234 0x789abc 123abc 0x78abg
^^^^
sdk/foo/foo.fidl: 10:4: Number <1234>
1234 0x789abc 123abc 0x78abg
     ^^^^^^^^
sdk/foo/foo.fidl: 10:9: Number <0x789abc>
1234 0x789abc 123abc 0x78abg
              ^^^^^^
sdk/foo/foo.fidl: 10:18: Name <123abc>
1234 0x789abc 123abc 0x78abg
                     ^^^^^^^
sdk/foo/foo.fidl: 10:25: Name <0x78abg>
"
        );
    }

    #[test]
    fn lexer_names() {
        let mut compiler = DocCompiler::new();
        let source = Rc::new(Source::new(
            "sdk/foo/foo.fidl".to_owned(),
            10,
            4,
            "It's correct to use heap-specific.\n".to_owned(),
        ));
        let items = reduce_lexems(&mut compiler, &source);
        assert!(!items.is_none());
        lexical_items_to_errors(&mut compiler, &items.unwrap(), /*with_spaces=*/ false);
        assert_eq!(
            compiler.errors,
            "\
It's correct to use heap-specific.
^^^^
sdk/foo/foo.fidl: 10:4: Name <It's>
It's correct to use heap-specific.
     ^^^^^^^
sdk/foo/foo.fidl: 10:9: Name <correct>
It's correct to use heap-specific.
             ^^
sdk/foo/foo.fidl: 10:17: Name <to>
It's correct to use heap-specific.
                ^^^
sdk/foo/foo.fidl: 10:20: Name <use>
It's correct to use heap-specific.
                    ^^^^^^^^^^^^^
sdk/foo/foo.fidl: 10:24: Name <heap-specific>
It's correct to use heap-specific.
                                 ^
sdk/foo/foo.fidl: 10:37: EndOfSentence <.>
"
        );
    }

    #[test]
    fn lexer_reference() {
        let mut compiler = DocCompiler::new();
        let source = Rc::new(Source::new(
            "sdk/foo/foo.fidl".to_owned(),
            10,
            4,
            "`xyz` isn't `abc`.".to_owned(),
        ));
        let items = reduce_lexems(&mut compiler, &source);
        assert!(!items.is_none());
        lexical_items_to_errors(&mut compiler, &items.unwrap(), /*with_spaces=*/ false);
        assert_eq!(
            compiler.errors,
            "\
`xyz` isn't `abc`.
 ^^^
sdk/foo/foo.fidl: 10:5: Reference <xyz>
`xyz` isn't `abc`.
      ^^^^^
sdk/foo/foo.fidl: 10:10: Name <isn't>
`xyz` isn't `abc`.
             ^^^
sdk/foo/foo.fidl: 10:17: Reference <abc>
`xyz` isn't `abc`.
                 ^
sdk/foo/foo.fidl: 10:21: EndOfSentence <.>
"
        );
    }

    #[test]
    fn lexer_spaces_and_new_lines() {
        let mut compiler = DocCompiler::new();
        let source = Rc::new(Source::new(
            "sdk/foo/foo.fidl".to_owned(),
            10,
            4,
            "Some     documentation.\n\n\nAnd   spaces.".to_owned(),
        ));
        let items = reduce_lexems(&mut compiler, &source);
        assert!(!items.is_none());
        lexical_items_to_errors(&mut compiler, &items.unwrap(), /*with_spaces=*/ true);
        assert_eq!(
            compiler.errors,
            "\
Some     documentation.
^^^^
sdk/foo/foo.fidl: 10:4: Name <Some>
Some     documentation.
    ^^^^^
sdk/foo/foo.fidl: 10:8: Spaces (5)
Some     documentation.
         ^^^^^^^^^^^^^
sdk/foo/foo.fidl: 10:13: Name <documentation>
Some     documentation.
                      ^
sdk/foo/foo.fidl: 10:26: EndOfSentence <.>
Some     documentation.
                       ^
sdk/foo/foo.fidl: 10:27: NewLines (3)
And   spaces.
^^^
sdk/foo/foo.fidl: 13:4: Name <And>
And   spaces.
   ^^^
sdk/foo/foo.fidl: 13:7: Spaces (3)
And   spaces.
      ^^^^^^
sdk/foo/foo.fidl: 13:10: Name <spaces>
And   spaces.
            ^
sdk/foo/foo.fidl: 13:16: EndOfSentence <.>
And   spaces.
             ^
sdk/foo/foo.fidl: 13:17: End
"
        );
    }

    #[test]
    fn lexer_bad_character() {
        let mut compiler = DocCompiler::new();
        let source = Rc::new(Source::new(
            "sdk/foo/foo.fidl".to_owned(),
            10,
            4,
            "Some $documentation.\n".to_owned(),
        ));
        let items = reduce_lexems(&mut compiler, &source);
        assert!(items.is_none());
        assert_eq!(
            compiler.errors,
            "\
Some $documentation.
     ^
sdk/foo/foo.fidl: 10:9: Unknown character <$>
"
        );
    }
}
