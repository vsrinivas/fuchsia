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
#[derive(Clone)]
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

    /// A block of text between two triple back quotes.
    CodeBlock(String),

    /// A text between single quotes. The left quote must be after a white space or at the
    /// beginning of the text. The text inside the quotes can't start with a space (in that
    /// particular case, it's a stand alone single quote).
    SingleQuoteString(String),

    /// A text between double quotes. The text inside the quotes can't start with a space (in
    /// that particular case, it's a stand alone double quote).
    DoubleQuoteString(String),

    /// A standalone single quote.
    SingleQuote,

    /// A standalone double quote.
    DoubleQuote,

    /// A comma.
    Comma,

    /// A semicolon.
    Semicolon,

    /// The plus character.
    Plus,

    /// The minus character.
    Minus,

    /// The asterisk character.
    Asterisk,

    /// The slash character (not immediately following a name).
    Slash,

    /// The percent character.
    Percent,

    /// The backslash character.
    BackSlash,

    /// The ampersand character.
    Ampersand,

    /// The hash (number sign) character.
    Hash,

    /// Two hash characters in a row.
    HashHash,

    /// The pipe character.
    Pipe,

    /// The tilde character.
    Tilde,

    /// The caret character.
    Caret,

    /// The dollar character.
    Dollar,

    /// The at sign character.
    AtSign,

    /// The unicode paragraph character.
    Paragraph,

    /// The equal character.
    Equal,

    /// Two equal characters in a row.
    EqualEqual,

    /// The left angle bracket.
    LowerThan,

    // The left angle bracket followed by an equal.
    LowerOrEqual,

    // The right angle bracket.
    GreaterThan,

    // The right angle bracket followed by an equal.
    GreaterOrEqual,

    /// A left parenthesis.
    LeftParenthesis,

    /// A Right parenthesis.
    RightParenthesis,

    /// A left bracket (square).
    LeftBracket,

    /// A Right bracket (square).
    RightBracket,

    /// A left brace (curly bracket).
    LeftBrace,

    /// A Right brace (curly bracket).
    RightBrace,
    /// A standalone unicode symbol like: ⮬ or ⮯.
    UnicodeCharacter(char),

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
#[derive(Clone)]
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
            '`' => reduce_back_quote(compiler, &mut items, &source, &mut iter),
            '\'' => {
                reduce_single_quote_string(compiler, &mut items, &source, index, &mut iter, &mut ok)
            }
            '"' => {
                reduce_double_quote_string(compiler, &mut items, &source, index, &mut iter, &mut ok)
            }
            ',' => reduce_single_character(
                &mut items,
                &source,
                index,
                &mut iter,
                LexicalContent::Comma,
            ),
            ';' => reduce_single_character(
                &mut items,
                &source,
                index,
                &mut iter,
                LexicalContent::Semicolon,
            ),
            '+' => {
                reduce_single_character(&mut items, &source, index, &mut iter, LexicalContent::Plus)
            }
            '-' => reduce_single_character(
                &mut items,
                &source,
                index,
                &mut iter,
                LexicalContent::Minus,
            ),
            '*' => reduce_single_character(
                &mut items,
                &source,
                index,
                &mut iter,
                LexicalContent::Asterisk,
            ),
            '/' => reduce_single_character(
                &mut items,
                &source,
                index,
                &mut iter,
                LexicalContent::Slash,
            ),
            '%' => reduce_single_character(
                &mut items,
                &source,
                index,
                &mut iter,
                LexicalContent::Percent,
            ),
            '\\' => reduce_single_character(
                &mut items,
                &source,
                index,
                &mut iter,
                LexicalContent::BackSlash,
            ),
            '&' => reduce_single_character(
                &mut items,
                &source,
                index,
                &mut iter,
                LexicalContent::Ampersand,
            ),
            '#' => reduce_one_or_two_characters(
                &mut items,
                &source,
                index,
                &mut iter,
                LexicalContent::Hash,
                '#',
                LexicalContent::HashHash,
            ),
            '|' => {
                reduce_single_character(&mut items, &source, index, &mut iter, LexicalContent::Pipe)
            }
            '~' => reduce_single_character(
                &mut items,
                &source,
                index,
                &mut iter,
                LexicalContent::Tilde,
            ),
            '^' => reduce_single_character(
                &mut items,
                &source,
                index,
                &mut iter,
                LexicalContent::Caret,
            ),
            '$' => reduce_single_character(
                &mut items,
                &source,
                index,
                &mut iter,
                LexicalContent::Dollar,
            ),
            '@' => reduce_single_character(
                &mut items,
                &source,
                index,
                &mut iter,
                LexicalContent::AtSign,
            ),
            '§' => reduce_single_character(
                &mut items,
                &source,
                index,
                &mut iter,
                LexicalContent::Paragraph,
            ),
            '=' => reduce_one_or_two_characters(
                &mut items,
                &source,
                index,
                &mut iter,
                LexicalContent::Equal,
                '=',
                LexicalContent::EqualEqual,
            ),
            '<' => reduce_one_or_two_characters(
                &mut items,
                &source,
                index,
                &mut iter,
                LexicalContent::LowerThan,
                '=',
                LexicalContent::LowerOrEqual,
            ),
            '>' => reduce_one_or_two_characters(
                &mut items,
                &source,
                index,
                &mut iter,
                LexicalContent::GreaterThan,
                '=',
                LexicalContent::GreaterOrEqual,
            ),
            '(' => reduce_single_character(
                &mut items,
                &source,
                index,
                &mut iter,
                LexicalContent::LeftParenthesis,
            ),
            ')' => reduce_single_character(
                &mut items,
                &source,
                index,
                &mut iter,
                LexicalContent::RightParenthesis,
            ),
            '[' => reduce_single_character(
                &mut items,
                &source,
                index,
                &mut iter,
                LexicalContent::LeftBracket,
            ),
            ']' => reduce_single_character(
                &mut items,
                &source,
                index,
                &mut iter,
                LexicalContent::RightBracket,
            ),
            '{' => reduce_single_character(
                &mut items,
                &source,
                index,
                &mut iter,
                LexicalContent::LeftBrace,
            ),
            '}' => reduce_single_character(
                &mut items,
                &source,
                index,
                &mut iter,
                LexicalContent::RightBrace,
            ),
            '⮬' => reduce_unicode_character(&mut items, &source, index, &mut iter, character),
            '⮯' => reduce_unicode_character(&mut items, &source, index, &mut iter, character),
            '⮫' => reduce_unicode_character(&mut items, &source, index, &mut iter, character),
            '⮨' => reduce_unicode_character(&mut items, &source, index, &mut iter, character),
            '⮭' => reduce_unicode_character(&mut items, &source, index, &mut iter, character),
            '⮮' => reduce_unicode_character(&mut items, &source, index, &mut iter, character),
            '⮪' => reduce_unicode_character(&mut items, &source, index, &mut iter, character),
            '⮩' => reduce_unicode_character(&mut items, &source, index, &mut iter, character),
            '↵' => reduce_unicode_character(&mut items, &source, index, &mut iter, character),
            '⌘' => reduce_unicode_character(&mut items, &source, index, &mut iter, character),
            // Reduces a sentence end character.
            '.' | ':' | '!' | '?' => {
                items.push(LexicalItem {
                    location: Location { source: Rc::clone(&source), start: index, end: index + 1 },
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

/// Reduces an item which starts with a back quote.
///
/// It can be a reference (text between two back quotes).
/// It can be a code block (text between two triple back quotes).
fn reduce_back_quote(
    compiler: &mut DocCompiler,
    items: &mut Vec<LexicalItem>,
    source: &Rc<Source>,
    iter: &mut CharIndices<'_>,
) -> Option<(usize, char)> {
    let mut current = iter.next();
    let start = if let Some((index, _)) = current { index } else { 0 };
    // A fist back quote has already been reduced. Look for a second one.
    if let Some((_, character)) = current {
        if character == '`' {
            current = iter.next();
            // Two back quotes have already been reduced. Look for a third one.
            let end = if let Some((index, character)) = current {
                if character == '`' {
                    // Three back quotes have been found. That means this is a code block.
                    return reduce_code_block(compiler, items, source, iter);
                }
                index
            } else {
                source.text.len()
            };
            // Only two back quotes have been found. It would be an empty reference. That means an
            // error.
            compiler.add_error(
                &Location { source: Rc::clone(&source), start, end },
                "Empty reference".to_owned(),
            );
            return current;
        }
    }
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

/// Reduces a block of text within two triple back quotes.
///
/// The opening triple back quotes have already been reduced.
fn reduce_code_block(
    compiler: &mut DocCompiler,
    items: &mut Vec<LexicalItem>,
    source: &Rc<Source>,
    iter: &mut CharIndices<'_>,
) -> Option<(usize, char)> {
    let mut current = iter.next();
    let start = if let Some((index, _)) = current { index } else { 0 };
    // Skip the characters between the opening three back quotes and the end of the line (these
    // characters define the code syntax to use).
    loop {
        match current {
            Some((_, character)) => {
                if character == '\n' {
                    current = iter.next();
                    break;
                }
            }
            None => {
                compiler.add_error(
                    &Location { source: Rc::clone(&source), start, end: source.text.len() },
                    "Unterminated code block".to_owned(),
                );
                return current;
            }
        }
        current = iter.next();
    }
    // Reduce the code block. The end of the block is reached when three back quotes followed by a
    // new line are found at the beginning of a line.
    // If three back quote are found at the beginning of a line without a following new line, this
    // is an error.
    let start_block = if let Some((index, _)) = current { index } else { 0 };
    loop {
        match current {
            Some((_, character)) => {
                if character != '\n' {
                    current = iter.next();
                    continue;
                }
                // A new line is found.
                current = iter.next();
                if current.is_none() {
                    continue;
                }
                let (end_block, first_quote) = current.unwrap();
                if first_quote != '`' {
                    continue;
                }
                // A fist back quote is found.
                current = iter.next();
                if current.is_none() {
                    continue;
                }
                let (_, second_quote) = current.unwrap();
                if second_quote != '`' {
                    continue;
                }
                // A second back quote is found.
                current = iter.next();
                if current.is_none() {
                    continue;
                }
                let (_, third_quote) = current.unwrap();
                if third_quote != '`' {
                    continue;
                }
                // A third back quote is found.
                current = iter.next();
                let new_line_pos = if let Some((index, ending_new_line)) = current {
                    if ending_new_line == '\n' {
                        // A following new line is found. We have reduced a code block.
                        items.push(LexicalItem {
                            location: Location {
                                source: Rc::clone(&source),
                                start: start_block,
                                end: end_block,
                            },
                            content: LexicalContent::CodeBlock(
                                source.text[start_block..end_block].to_string(),
                            ),
                        });
                        return iter.next();
                    }
                    index
                } else {
                    source.text.len()
                };
                // The three back quote are not followed by a new line. This is an error.
                compiler.add_error(
                    &Location {
                        source: Rc::clone(&source),
                        start: new_line_pos,
                        end: new_line_pos,
                    },
                    "New line expected to end the code block".to_owned(),
                );
                return current;
            }
            None => {
                compiler.add_error(
                    &Location { source: Rc::clone(&source), start, end: source.text.len() },
                    "Unterminated code block".to_owned(),
                );
                break;
            }
        }
    }
    current
}

/// Reduces a string between single quotes, a single quote or a name.
///
/// If the first single quote is not after a white space or at the beginning of the text then,
/// a Name is returned.
/// For example, with the input: `a_reference`'s
/// This function is called to reduce 's and the result is Name("'s").
///
/// If the first single quote is followed by a space, the result is SingleQuote.
/// For example, the following text is valid: A ' is valid.
fn reduce_single_quote_string(
    compiler: &mut DocCompiler,
    items: &mut Vec<LexicalItem>,
    source: &Rc<Source>,
    start: usize,
    iter: &mut CharIndices<'_>,
    ok: &mut bool,
) -> Option<(usize, char)> {
    // If the quote is not at the beginning of the line and not preceded by a white space then
    // reduce a Name.
    if let Some(last) = items.last() {
        match last.content {
            LexicalContent::Spaces(_) | LexicalContent::NewLines(_) => {}
            _ => return reduce_name(items, source, start, iter),
        }
    }
    // If the quote is immediately followed by a space then only reduce a SingleQuote.
    let mut current = iter.next();
    let text_start = if let Some((index, character)) = current {
        if character == ' ' || character == '\n' {
            items.push(LexicalItem {
                location: Location { source: Rc::clone(&source), start, end: start },
                content: LexicalContent::SingleQuote,
            });
            return current;
        }
        index
    } else {
        0
    };
    // Reduces a string.
    loop {
        match current {
            Some((index, character)) => match character {
                '\'' => {
                    current = iter.next();
                    let end =
                        if let Some((index, _)) = current { index } else { source.text.len() };
                    items.push(LexicalItem {
                        location: Location { source: Rc::clone(&source), start, end: end },
                        content: LexicalContent::SingleQuoteString(
                            source.text[text_start..index].to_string(),
                        ),
                    });
                    return current;
                }
                _ => {}
            },
            None => {
                break;
            }
        }
        current = iter.next();
    }
    *ok = false;
    compiler.add_error(
        &Location { source: Rc::clone(&source), start, end: source.text.len() },
        "Unterminated string (character <'> expected).".to_owned(),
    );
    current
}

/// Reduces a string between double quotes or a double quote.
///
/// If the first double quote is followed by a space, the result is DoubleQuote.
/// For example, the following text is valid: A " is valid.
fn reduce_double_quote_string(
    compiler: &mut DocCompiler,
    items: &mut Vec<LexicalItem>,
    source: &Rc<Source>,
    start: usize,
    iter: &mut CharIndices<'_>,
    ok: &mut bool,
) -> Option<(usize, char)> {
    let mut current = iter.next();
    // If the quote is immediately followed by a space then only reduce a DoubleQuote.
    let text_start = if let Some((index, character)) = current {
        if character == ' ' || character == '\n' {
            items.push(LexicalItem {
                location: Location { source: Rc::clone(&source), start, end: start },
                content: LexicalContent::DoubleQuote,
            });
            return current;
        }
        index
    } else {
        0
    };
    // Reduces a string.
    loop {
        match current {
            Some((index, character)) => match character {
                '"' => {
                    current = iter.next();
                    let end =
                        if let Some((index, _)) = current { index } else { source.text.len() };
                    items.push(LexicalItem {
                        location: Location { source: Rc::clone(&source), start, end: end },
                        content: LexicalContent::DoubleQuoteString(
                            source.text[text_start..index].to_string(),
                        ),
                    });
                    return current;
                }
                _ => {}
            },
            None => {
                break;
            }
        }
        current = iter.next();
    }
    *ok = false;
    compiler.add_error(
        &Location { source: Rc::clone(&source), start, end: source.text.len() },
        "Unterminated string (character <\"> expected).".to_owned(),
    );
    current
}

/// Reduce a single character.
///
/// The caller already found what character it is. Only adds the content to items and returns the
/// next character to reduce.
fn reduce_single_character(
    items: &mut Vec<LexicalItem>,
    source: &Rc<Source>,
    start: usize,
    iter: &mut CharIndices<'_>,
    content: LexicalContent,
) -> Option<(usize, char)> {
    items.push(LexicalItem {
        location: Location { source: Rc::clone(&source), start, end: start },
        content,
    });
    iter.next()
}

/// Reduce one or two characters.
///
/// Add either one_character_content or two_character_content to items based on whether the next
/// character matches second_character.
fn reduce_one_or_two_characters(
    items: &mut Vec<LexicalItem>,
    source: &Rc<Source>,
    start: usize,
    iter: &mut CharIndices<'_>,
    one_character_content: LexicalContent,
    second_character: char,
    two_character_content: LexicalContent,
) -> Option<(usize, char)> {
    let current = iter.next();
    if let Some((_, character)) = current {
        if character == second_character {
            let current = iter.next();
            let end = if let Some((index, _)) = current { index } else { source.text.len() };
            items.push(LexicalItem {
                location: Location { source: Rc::clone(&source), start, end },
                content: two_character_content,
            });
            return current;
        }
    }
    items.push(LexicalItem {
        location: Location { source: Rc::clone(&source), start, end: start },
        content: one_character_content,
    });
    current
}

/// Reduce a single unicode character.
fn reduce_unicode_character(
    items: &mut Vec<LexicalItem>,
    source: &Rc<Source>,
    start: usize,
    iter: &mut CharIndices<'_>,
    character: char,
) -> Option<(usize, char)> {
    items.push(LexicalItem {
        location: Location { source: Rc::clone(&source), start, end: start },
        content: LexicalContent::UnicodeCharacter(character),
    });
    iter.next()
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
    use crate::source::Source;
    use crate::utils::test::lexical_items_to_errors;
    use crate::DocCompiler;
    use std::rc::Rc;

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
    fn lexer_reference_with_apostrophe() {
        let mut compiler = DocCompiler::new();
        let source =
            Rc::new(Source::new("sdk/foo/foo.fidl".to_owned(), 10, 4, "`xyz`'s.".to_owned()));
        let items = reduce_lexems(&mut compiler, &source);
        assert!(!items.is_none());
        lexical_items_to_errors(&mut compiler, &items.unwrap(), /*with_spaces=*/ false);
        assert_eq!(
            compiler.errors,
            "\
`xyz`'s.
 ^^^
sdk/foo/foo.fidl: 10:5: Reference <xyz>
`xyz`'s.
     ^^
sdk/foo/foo.fidl: 10:9: Name <'s>
`xyz`'s.
       ^
sdk/foo/foo.fidl: 10:11: EndOfSentence <.>
"
        );
    }

    #[test]
    fn lexer_reference_with_apostrophe_at_the_end() {
        let mut compiler = DocCompiler::new();
        let source =
            Rc::new(Source::new("sdk/foo/foo.fidl".to_owned(), 10, 4, "`xyz`'s".to_owned()));
        let items = reduce_lexems(&mut compiler, &source);
        assert!(!items.is_none());
        lexical_items_to_errors(&mut compiler, &items.unwrap(), /*with_spaces=*/ false);
        assert_eq!(
            compiler.errors,
            "\
`xyz`'s
 ^^^
sdk/foo/foo.fidl: 10:5: Reference <xyz>
`xyz`'s
     ^^
sdk/foo/foo.fidl: 10:9: Name <'s>
"
        );
    }

    #[test]
    fn lexer_empty_reference() {
        let mut compiler = DocCompiler::new();
        let source =
            Rc::new(Source::new("sdk/foo/foo.fidl".to_owned(), 10, 4, "`` abc".to_owned()));
        let _items = reduce_lexems(&mut compiler, &source);
        assert_eq!(
            compiler.errors,
            "\
`` abc
 ^
sdk/foo/foo.fidl: 10:5: Empty reference
"
        );
    }

    #[test]
    fn lexer_code_block() {
        let mut compiler = DocCompiler::new();
        let source = Rc::new(Source::new(
            "sdk/foo/foo.fidl".to_owned(),
            10,
            4,
            "```c++\nint a;\n```\n".to_owned(),
        ));
        let items = reduce_lexems(&mut compiler, &source);
        assert!(!items.is_none());
        lexical_items_to_errors(&mut compiler, &items.unwrap(), /*with_spaces=*/ false);
        assert_eq!(
            compiler.errors,
            "\
int a;
^^^^^^
sdk/foo/foo.fidl: 11:4: CodeBlock <int a;
>
"
        );
    }

    #[test]
    fn lexer_unterminated_code_block_1() {
        let mut compiler = DocCompiler::new();
        let source =
            Rc::new(Source::new("sdk/foo/foo.fidl".to_owned(), 10, 4, "```c++".to_owned()));
        let _items = reduce_lexems(&mut compiler, &source);
        assert_eq!(
            compiler.errors,
            "\
```c++
   ^^^
sdk/foo/foo.fidl: 10:7: Unterminated code block
"
        );
    }

    #[test]
    fn lexer_unterminated_code_block_2() {
        let mut compiler = DocCompiler::new();
        let source = Rc::new(Source::new(
            "sdk/foo/foo.fidl".to_owned(),
            10,
            4,
            "```c++\nint a;\n".to_owned(),
        ));
        let _items = reduce_lexems(&mut compiler, &source);
        assert_eq!(
            compiler.errors,
            "\
```c++
   ^^^
sdk/foo/foo.fidl: 10:7: Unterminated code block
"
        );
    }

    #[test]
    fn lexer_code_block_missing_ending_new_line_1() {
        let mut compiler = DocCompiler::new();
        let source = Rc::new(Source::new(
            "sdk/foo/foo.fidl".to_owned(),
            10,
            4,
            "```c++\nint a;\n```".to_owned(),
        ));
        let _items = reduce_lexems(&mut compiler, &source);
        assert_eq!(
            compiler.errors,
            "\
```
   ^
sdk/foo/foo.fidl: 12:7: New line expected to end the code block
"
        );
    }

    #[test]
    fn lexer_code_block_missing_ending_new_line_2() {
        let mut compiler = DocCompiler::new();
        let source = Rc::new(Source::new(
            "sdk/foo/foo.fidl".to_owned(),
            10,
            4,
            "```c++\nint a;\n```xyz".to_owned(),
        ));
        let _items = reduce_lexems(&mut compiler, &source);
        assert_eq!(
            compiler.errors,
            "\
```xyz
   ^
sdk/foo/foo.fidl: 12:7: New line expected to end the code block
"
        );
    }

    #[test]
    fn lexer_single_quotes() {
        let mut compiler = DocCompiler::new();
        let source = Rc::new(Source::new(
            "sdk/foo/foo.fidl".to_owned(),
            10,
            4,
            "'abcd' ' abc's 'xyz' '' `abc`'s".to_owned(),
        ));
        let items = reduce_lexems(&mut compiler, &source);
        assert!(!items.is_none());
        lexical_items_to_errors(&mut compiler, &items.unwrap(), /*with_spaces=*/ false);
        assert_eq!(
            compiler.errors,
            "\
'abcd' ' abc's 'xyz' '' `abc`'s
^^^^^^
sdk/foo/foo.fidl: 10:4: SingleQuoteString <abcd>
'abcd' ' abc's 'xyz' '' `abc`'s
       ^
sdk/foo/foo.fidl: 10:11: SingleQuote
'abcd' ' abc's 'xyz' '' `abc`'s
         ^^^^^
sdk/foo/foo.fidl: 10:13: Name <abc's>
'abcd' ' abc's 'xyz' '' `abc`'s
               ^^^^^
sdk/foo/foo.fidl: 10:19: SingleQuoteString <xyz>
'abcd' ' abc's 'xyz' '' `abc`'s
                     ^^
sdk/foo/foo.fidl: 10:25: SingleQuoteString <>
'abcd' ' abc's 'xyz' '' `abc`'s
                         ^^^
sdk/foo/foo.fidl: 10:29: Reference <abc>
'abcd' ' abc's 'xyz' '' `abc`'s
                             ^^
sdk/foo/foo.fidl: 10:33: Name <'s>
"
        );
    }

    #[test]
    /// Checks that we can have a backslashes including at the end of a string
    fn lexer_single_quotes_with_backslash() {
        let mut compiler = DocCompiler::new();
        let source =
            Rc::new(Source::new("sdk/foo/foo.fidl".to_owned(), 10, 4, "'xxx\\ \\'".to_owned()));
        let items = reduce_lexems(&mut compiler, &source);
        assert!(!items.is_none());
        lexical_items_to_errors(&mut compiler, &items.unwrap(), /*with_spaces=*/ false);
        assert_eq!(
            compiler.errors,
            "\
'xxx\\ \\'
^^^^^^^^
sdk/foo/foo.fidl: 10:4: SingleQuoteString <xxx\\ \\>
"
        );
    }

    #[test]
    fn lexer_unterminated_single_quotes_1() {
        let mut compiler = DocCompiler::new();
        let source = Rc::new(Source::new("sdk/foo/foo.fidl".to_owned(), 10, 4, "'abcd".to_owned()));
        let items = reduce_lexems(&mut compiler, &source);
        assert!(items.is_none());
        assert_eq!(
            compiler.errors,
            "\
'abcd
^^^^^
sdk/foo/foo.fidl: 10:4: Unterminated string (character <'> expected).
"
        );
    }

    #[test]
    fn lexer_unterminated_single_quotes_2() {
        let mut compiler = DocCompiler::new();
        let source =
            Rc::new(Source::new("sdk/foo/foo.fidl".to_owned(), 10, 4, "'abcd\\".to_owned()));
        let items = reduce_lexems(&mut compiler, &source);
        assert!(items.is_none());
        assert_eq!(
            compiler.errors,
            "\
'abcd\\
^^^^^^
sdk/foo/foo.fidl: 10:4: Unterminated string (character <'> expected).
"
        );
    }

    #[test]
    fn lexer_double_quotes() {
        let mut compiler = DocCompiler::new();
        let source = Rc::new(Source::new(
            "sdk/foo/foo.fidl".to_owned(),
            10,
            4,
            "\"abcd\" \" \"\" \"xyz\"".to_owned(),
        ));
        let items = reduce_lexems(&mut compiler, &source);
        assert!(!items.is_none());
        lexical_items_to_errors(&mut compiler, &items.unwrap(), /*with_spaces=*/ false);
        assert_eq!(
            compiler.errors,
            "\
\"abcd\" \" \"\" \"xyz\"
^^^^^^
sdk/foo/foo.fidl: 10:4: DoubleQuoteString <abcd>
\"abcd\" \" \"\" \"xyz\"
       ^
sdk/foo/foo.fidl: 10:11: DoubleQuote
\"abcd\" \" \"\" \"xyz\"
         ^^
sdk/foo/foo.fidl: 10:13: DoubleQuoteString <>
\"abcd\" \" \"\" \"xyz\"
            ^^^^^
sdk/foo/foo.fidl: 10:16: DoubleQuoteString <xyz>
"
        );
    }

    #[test]
    /// Checks that we can have a backslashes including at the end of a string
    fn lexer_double_quotes_with_backslash() {
        let mut compiler = DocCompiler::new();
        let source =
            Rc::new(Source::new("sdk/foo/foo.fidl".to_owned(), 10, 4, "\"xxx\\ \\\"".to_owned()));
        let items = reduce_lexems(&mut compiler, &source);
        assert!(!items.is_none());
        lexical_items_to_errors(&mut compiler, &items.unwrap(), /*with_spaces=*/ false);
        assert_eq!(
            compiler.errors,
            "\
\"xxx\\ \\\"
^^^^^^^^
sdk/foo/foo.fidl: 10:4: DoubleQuoteString <xxx\\ \\>
"
        );
    }

    #[test]
    fn lexer_unterminated_double_quotes_1() {
        let mut compiler = DocCompiler::new();
        let source =
            Rc::new(Source::new("sdk/foo/foo.fidl".to_owned(), 10, 4, "\"abcd".to_owned()));
        let items = reduce_lexems(&mut compiler, &source);
        assert!(items.is_none());
        assert_eq!(
            compiler.errors,
            "\
\"abcd
^^^^^
sdk/foo/foo.fidl: 10:4: Unterminated string (character <\"> expected).
"
        );
    }

    #[test]
    fn lexer_unterminated_double_quotes_2() {
        let mut compiler = DocCompiler::new();
        let source =
            Rc::new(Source::new("sdk/foo/foo.fidl".to_owned(), 10, 4, "\"abcd\\".to_owned()));
        let items = reduce_lexems(&mut compiler, &source);
        assert!(items.is_none());
        assert_eq!(
            compiler.errors,
            "\
\"abcd\\
^^^^^^
sdk/foo/foo.fidl: 10:4: Unterminated string (character <\"> expected).
"
        );
    }

    #[test]
    fn lexer_symbols_and_punctuation() {
        let mut compiler = DocCompiler::new();
        let source = Rc::new(Source::new(
            "sdk/foo/foo.fidl".to_owned(),
            10,
            4,
            ", ; + - * / % & # ## | ~ ^ $ @ § = == < <= > >= ( ) [ ] { } \\".to_owned(),
        ));
        let items = reduce_lexems(&mut compiler, &source);
        assert!(!items.is_none());
        lexical_items_to_errors(&mut compiler, &items.unwrap(), /*with_spaces=*/ false);
        assert_eq!(
            compiler.errors,
            "\
, ; + - * / % & # ## | ~ ^ $ @ § = == < <= > >= ( ) [ ] { } \\
^
sdk/foo/foo.fidl: 10:4: Comma
, ; + - * / % & # ## | ~ ^ $ @ § = == < <= > >= ( ) [ ] { } \\
  ^
sdk/foo/foo.fidl: 10:6: Semicolon
, ; + - * / % & # ## | ~ ^ $ @ § = == < <= > >= ( ) [ ] { } \\
    ^
sdk/foo/foo.fidl: 10:8: Plus
, ; + - * / % & # ## | ~ ^ $ @ § = == < <= > >= ( ) [ ] { } \\
      ^
sdk/foo/foo.fidl: 10:10: Minus
, ; + - * / % & # ## | ~ ^ $ @ § = == < <= > >= ( ) [ ] { } \\
        ^
sdk/foo/foo.fidl: 10:12: Asterisk
, ; + - * / % & # ## | ~ ^ $ @ § = == < <= > >= ( ) [ ] { } \\
          ^
sdk/foo/foo.fidl: 10:14: Slash
, ; + - * / % & # ## | ~ ^ $ @ § = == < <= > >= ( ) [ ] { } \\
            ^
sdk/foo/foo.fidl: 10:16: Percent
, ; + - * / % & # ## | ~ ^ $ @ § = == < <= > >= ( ) [ ] { } \\
              ^
sdk/foo/foo.fidl: 10:18: Ampersand
, ; + - * / % & # ## | ~ ^ $ @ § = == < <= > >= ( ) [ ] { } \\
                ^
sdk/foo/foo.fidl: 10:20: Hash
, ; + - * / % & # ## | ~ ^ $ @ § = == < <= > >= ( ) [ ] { } \\
                  ^^
sdk/foo/foo.fidl: 10:22: HashHash
, ; + - * / % & # ## | ~ ^ $ @ § = == < <= > >= ( ) [ ] { } \\
                     ^
sdk/foo/foo.fidl: 10:25: Pipe
, ; + - * / % & # ## | ~ ^ $ @ § = == < <= > >= ( ) [ ] { } \\
                       ^
sdk/foo/foo.fidl: 10:27: Tilde
, ; + - * / % & # ## | ~ ^ $ @ § = == < <= > >= ( ) [ ] { } \\
                         ^
sdk/foo/foo.fidl: 10:29: Caret
, ; + - * / % & # ## | ~ ^ $ @ § = == < <= > >= ( ) [ ] { } \\
                           ^
sdk/foo/foo.fidl: 10:31: Dollar
, ; + - * / % & # ## | ~ ^ $ @ § = == < <= > >= ( ) [ ] { } \\
                             ^
sdk/foo/foo.fidl: 10:33: AtSign
, ; + - * / % & # ## | ~ ^ $ @ § = == < <= > >= ( ) [ ] { } \\
                               ^
sdk/foo/foo.fidl: 10:35: Paragraph
, ; + - * / % & # ## | ~ ^ $ @ § = == < <= > >= ( ) [ ] { } \\
                                 ^
sdk/foo/foo.fidl: 10:37: Equal
, ; + - * / % & # ## | ~ ^ $ @ § = == < <= > >= ( ) [ ] { } \\
                                   ^^
sdk/foo/foo.fidl: 10:39: EqualEqual
, ; + - * / % & # ## | ~ ^ $ @ § = == < <= > >= ( ) [ ] { } \\
                                      ^
sdk/foo/foo.fidl: 10:42: LowerThan
, ; + - * / % & # ## | ~ ^ $ @ § = == < <= > >= ( ) [ ] { } \\
                                        ^^
sdk/foo/foo.fidl: 10:44: LowerOrEqual
, ; + - * / % & # ## | ~ ^ $ @ § = == < <= > >= ( ) [ ] { } \\
                                           ^
sdk/foo/foo.fidl: 10:47: GreaterThan
, ; + - * / % & # ## | ~ ^ $ @ § = == < <= > >= ( ) [ ] { } \\
                                             ^^
sdk/foo/foo.fidl: 10:49: GreaterOrEqual
, ; + - * / % & # ## | ~ ^ $ @ § = == < <= > >= ( ) [ ] { } \\
                                                ^
sdk/foo/foo.fidl: 10:52: LeftParenthesis
, ; + - * / % & # ## | ~ ^ $ @ § = == < <= > >= ( ) [ ] { } \\
                                                  ^
sdk/foo/foo.fidl: 10:54: RightParenthesis
, ; + - * / % & # ## | ~ ^ $ @ § = == < <= > >= ( ) [ ] { } \\
                                                    ^
sdk/foo/foo.fidl: 10:56: LeftBracket
, ; + - * / % & # ## | ~ ^ $ @ § = == < <= > >= ( ) [ ] { } \\
                                                      ^
sdk/foo/foo.fidl: 10:58: RightBracket
, ; + - * / % & # ## | ~ ^ $ @ § = == < <= > >= ( ) [ ] { } \\
                                                        ^
sdk/foo/foo.fidl: 10:60: LeftBrace
, ; + - * / % & # ## | ~ ^ $ @ § = == < <= > >= ( ) [ ] { } \\
                                                          ^
sdk/foo/foo.fidl: 10:62: RightBrace
, ; + - * / % & # ## | ~ ^ $ @ § = == < <= > >= ( ) [ ] { } \\
                                                            ^
sdk/foo/foo.fidl: 10:64: BackSlash
"
        );
    }

    #[test]
    fn lexer_unicode_symbols() {
        let mut compiler = DocCompiler::new();
        let source = Rc::new(Source::new(
            "sdk/foo/foo.fidl".to_owned(),
            10,
            4,
            "⮬ ⮯ ⮫ ⮨ ⮭ ⮮ ⮪ ⮩ ↵ ⌘".to_owned(),
        ));
        let items = reduce_lexems(&mut compiler, &source);
        assert!(!items.is_none());
        lexical_items_to_errors(&mut compiler, &items.unwrap(), /*with_spaces=*/ false);
        assert_eq!(
            compiler.errors,
            "\
⮬ ⮯ ⮫ ⮨ ⮭ ⮮ ⮪ ⮩ ↵ ⌘
^
sdk/foo/foo.fidl: 10:4: UnicodeCharacter <⮬>
⮬ ⮯ ⮫ ⮨ ⮭ ⮮ ⮪ ⮩ ↵ ⌘
  ^
sdk/foo/foo.fidl: 10:6: UnicodeCharacter <⮯>
⮬ ⮯ ⮫ ⮨ ⮭ ⮮ ⮪ ⮩ ↵ ⌘
    ^
sdk/foo/foo.fidl: 10:8: UnicodeCharacter <⮫>
⮬ ⮯ ⮫ ⮨ ⮭ ⮮ ⮪ ⮩ ↵ ⌘
      ^
sdk/foo/foo.fidl: 10:10: UnicodeCharacter <⮨>
⮬ ⮯ ⮫ ⮨ ⮭ ⮮ ⮪ ⮩ ↵ ⌘
        ^
sdk/foo/foo.fidl: 10:12: UnicodeCharacter <⮭>
⮬ ⮯ ⮫ ⮨ ⮭ ⮮ ⮪ ⮩ ↵ ⌘
          ^
sdk/foo/foo.fidl: 10:14: UnicodeCharacter <⮮>
⮬ ⮯ ⮫ ⮨ ⮭ ⮮ ⮪ ⮩ ↵ ⌘
            ^
sdk/foo/foo.fidl: 10:16: UnicodeCharacter <⮪>
⮬ ⮯ ⮫ ⮨ ⮭ ⮮ ⮪ ⮩ ↵ ⌘
              ^
sdk/foo/foo.fidl: 10:18: UnicodeCharacter <⮩>
⮬ ⮯ ⮫ ⮨ ⮭ ⮮ ⮪ ⮩ ↵ ⌘
                ^
sdk/foo/foo.fidl: 10:20: UnicodeCharacter <↵>
⮬ ⮯ ⮫ ⮨ ⮭ ⮮ ⮪ ⮩ ↵ ⌘
                  ^
sdk/foo/foo.fidl: 10:22: UnicodeCharacter <⌘>
"
        );
    }

    #[test]
    fn lexer_end_of_sentence() {
        let mut compiler = DocCompiler::new();
        let source = Rc::new(Source::new(
            "sdk/foo/foo.fidl".to_owned(),
            10,
            4,
            "Aa. Bb? Cc! Dd:".to_owned(),
        ));
        let items = reduce_lexems(&mut compiler, &source);
        assert!(!items.is_none());
        lexical_items_to_errors(&mut compiler, &items.unwrap(), /*with_spaces=*/ false);
        assert_eq!(
            compiler.errors,
            "\
Aa. Bb? Cc! Dd:
^^
sdk/foo/foo.fidl: 10:4: Name <Aa>
Aa. Bb? Cc! Dd:
  ^
sdk/foo/foo.fidl: 10:6: EndOfSentence <.>
Aa. Bb? Cc! Dd:
    ^^
sdk/foo/foo.fidl: 10:8: Name <Bb>
Aa. Bb? Cc! Dd:
      ^
sdk/foo/foo.fidl: 10:10: EndOfSentence <?>
Aa. Bb? Cc! Dd:
        ^^
sdk/foo/foo.fidl: 10:12: Name <Cc>
Aa. Bb? Cc! Dd:
          ^
sdk/foo/foo.fidl: 10:14: EndOfSentence <!>
Aa. Bb? Cc! Dd:
            ^^
sdk/foo/foo.fidl: 10:16: Name <Dd>
Aa. Bb? Cc! Dd:
              ^
sdk/foo/foo.fidl: 10:18: EndOfSentence <:>
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
        let source =
            Rc::new(Source::new("sdk/foo/foo.fidl".to_owned(), 10, 4, "En dash —.\n".to_owned()));
        let items = reduce_lexems(&mut compiler, &source);
        assert!(items.is_none());
        assert_eq!(
            compiler.errors,
            "\
En dash —.
        ^
sdk/foo/foo.fidl: 10:12: Unknown character <—>
"
        );
    }
}
