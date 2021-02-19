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
    /// Some text. It can be any text the lexer can isolate.
    /// For example: "hello", "+", "++", "&", ...
    Text(String),
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
            // Reduces a name.
            'a'..='z' | 'A'..='Z' | '_' => reduce_name(&mut items, &source, index, &mut iter),
            // Reduces spaces.
            ' ' => reduce_spaces(&mut items, &source, index, &mut iter),
            // Reduces a sentence end character.
            '.' | ':' | '!' | '?' => {
                items.push(LexicalItem {
                    location: Location { source: Rc::clone(&source), start: index, end: index },
                    content: LexicalContent::EndOfSentence(character),
                });
                iter.next()
            }
            // Reduces new lines.
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

/// Reduces a name.
///
/// A name is a valid name from the English language point of view.
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
        content: LexicalContent::Text(source.text[start..end].to_string()),
    });
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

    fn lexical_items_to_errors(compiler: &mut DocCompiler, items: &Vec<LexicalItem>) {
        for item in items.iter() {
            match &item.content {
                LexicalContent::Text(text) => {
                    compiler.add_error(&item.location, format!("Text <{}>", text))
                }
                LexicalContent::EndOfSentence(character) => {
                    compiler.add_error(&item.location, format!("EndOfSentence <{}>", character))
                }
                LexicalContent::Spaces(count) => {
                    compiler.add_error(&item.location, format!("Spaces ({})", count))
                }
                LexicalContent::NewLines(count) => {
                    compiler.add_error(&item.location, format!("NewLines ({})", count))
                }
                LexicalContent::EndOfInput => compiler.add_error(&item.location, "End".to_owned()),
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
    fn lexer_items() {
        let mut compiler = DocCompiler::new();
        let source = Rc::new(Source::new(
            "sdk/foo/foo.fidl".to_owned(),
            10,
            4,
            "Some documentation.\n".to_owned(),
        ));
        let items = reduce_lexems(&mut compiler, &source);
        assert!(!items.is_none());
        lexical_items_to_errors(&mut compiler, &items.unwrap());
        assert_eq!(
            compiler.errors,
            "\
Some documentation.
^^^^
sdk/foo/foo.fidl: 10:4: Text <Some>
Some documentation.
    ^
sdk/foo/foo.fidl: 10:8: Spaces (1)
Some documentation.
     ^^^^^^^^^^^^^
sdk/foo/foo.fidl: 10:9: Text <documentation>
Some documentation.
                  ^
sdk/foo/foo.fidl: 10:22: EndOfSentence <.>
Some documentation.
                   ^
sdk/foo/foo.fidl: 10:23: NewLines (1)

^
sdk/foo/foo.fidl: 11:4: End
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
        lexical_items_to_errors(&mut compiler, &items.unwrap());
        assert_eq!(
            compiler.errors,
            "\
It's correct to use heap-specific.
^^^^
sdk/foo/foo.fidl: 10:4: Text <It's>
It's correct to use heap-specific.
    ^
sdk/foo/foo.fidl: 10:8: Spaces (1)
It's correct to use heap-specific.
     ^^^^^^^
sdk/foo/foo.fidl: 10:9: Text <correct>
It's correct to use heap-specific.
            ^
sdk/foo/foo.fidl: 10:16: Spaces (1)
It's correct to use heap-specific.
             ^^
sdk/foo/foo.fidl: 10:17: Text <to>
It's correct to use heap-specific.
               ^
sdk/foo/foo.fidl: 10:19: Spaces (1)
It's correct to use heap-specific.
                ^^^
sdk/foo/foo.fidl: 10:20: Text <use>
It's correct to use heap-specific.
                   ^
sdk/foo/foo.fidl: 10:23: Spaces (1)
It's correct to use heap-specific.
                    ^^^^^^^^^^^^^
sdk/foo/foo.fidl: 10:24: Text <heap-specific>
It's correct to use heap-specific.
                                 ^
sdk/foo/foo.fidl: 10:37: EndOfSentence <.>
It's correct to use heap-specific.
                                  ^
sdk/foo/foo.fidl: 10:38: NewLines (1)

^
sdk/foo/foo.fidl: 11:4: End
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
        lexical_items_to_errors(&mut compiler, &items.unwrap());
        assert_eq!(
            compiler.errors,
            "\
Some     documentation.
^^^^
sdk/foo/foo.fidl: 10:4: Text <Some>
Some     documentation.
    ^^^^^
sdk/foo/foo.fidl: 10:8: Spaces (5)
Some     documentation.
         ^^^^^^^^^^^^^
sdk/foo/foo.fidl: 10:13: Text <documentation>
Some     documentation.
                      ^
sdk/foo/foo.fidl: 10:26: EndOfSentence <.>
Some     documentation.
                       ^
sdk/foo/foo.fidl: 10:27: NewLines (3)
And   spaces.
^^^
sdk/foo/foo.fidl: 13:4: Text <And>
And   spaces.
   ^^^
sdk/foo/foo.fidl: 13:7: Spaces (3)
And   spaces.
      ^^^^^^
sdk/foo/foo.fidl: 13:10: Text <spaces>
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
