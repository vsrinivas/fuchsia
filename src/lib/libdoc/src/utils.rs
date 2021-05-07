// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Defines utility functions.

/// Convert the lexical items to text to be able to check them.
#[cfg(test)]
pub mod test {
    use crate::lexer::LexicalContent;
    use crate::lexer::LexicalItem;
    use crate::DocCompiler;

    pub fn lexical_items_to_errors(
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
                LexicalContent::CodeBlock(text) => {
                    compiler.add_error(&item.location, format!("CodeBlock <{}>", text))
                }
                LexicalContent::SingleQuoteString(text) => {
                    compiler.add_error(&item.location, format!("SingleQuoteString <{}>", text))
                }
                LexicalContent::DoubleQuoteString(text) => {
                    compiler.add_error(&item.location, format!("DoubleQuoteString <{}>", text))
                }
                LexicalContent::SingleQuote => {
                    compiler.add_error(&item.location, "SingleQuote".to_owned())
                }
                LexicalContent::DoubleQuote => {
                    compiler.add_error(&item.location, "DoubleQuote".to_owned())
                }
                LexicalContent::Comma => compiler.add_error(&item.location, "Comma".to_owned()),
                LexicalContent::Semicolon => {
                    compiler.add_error(&item.location, "Semicolon".to_owned())
                }
                LexicalContent::Plus => compiler.add_error(&item.location, "Plus".to_owned()),
                LexicalContent::Minus => compiler.add_error(&item.location, "Minus".to_owned()),
                LexicalContent::Asterisk => {
                    compiler.add_error(&item.location, "Asterisk".to_owned())
                }
                LexicalContent::Slash => compiler.add_error(&item.location, "Slash".to_owned()),
                LexicalContent::Percent => compiler.add_error(&item.location, "Percent".to_owned()),
                LexicalContent::BackSlash => {
                    compiler.add_error(&item.location, "BackSlash".to_owned())
                }
                LexicalContent::Ampersand => {
                    compiler.add_error(&item.location, "Ampersand".to_owned())
                }
                LexicalContent::Hash => compiler.add_error(&item.location, "Hash".to_owned()),
                LexicalContent::HashHash => {
                    compiler.add_error(&item.location, "HashHash".to_owned())
                }
                LexicalContent::Pipe => compiler.add_error(&item.location, "Pipe".to_owned()),
                LexicalContent::Tilde => compiler.add_error(&item.location, "Tilde".to_owned()),
                LexicalContent::Caret => compiler.add_error(&item.location, "Caret".to_owned()),
                LexicalContent::Dollar => compiler.add_error(&item.location, "Dollar".to_owned()),
                LexicalContent::AtSign => compiler.add_error(&item.location, "AtSign".to_owned()),
                LexicalContent::Paragraph => {
                    compiler.add_error(&item.location, "Paragraph".to_owned())
                }
                LexicalContent::Equal => compiler.add_error(&item.location, "Equal".to_owned()),
                LexicalContent::EqualEqual => {
                    compiler.add_error(&item.location, "EqualEqual".to_owned())
                }
                LexicalContent::LowerThan => {
                    compiler.add_error(&item.location, "LowerThan".to_owned())
                }
                LexicalContent::LowerOrEqual => {
                    compiler.add_error(&item.location, "LowerOrEqual".to_owned())
                }
                LexicalContent::GreaterThan => {
                    compiler.add_error(&item.location, "GreaterThan".to_owned())
                }
                LexicalContent::GreaterOrEqual => {
                    compiler.add_error(&item.location, "GreaterOrEqual".to_owned())
                }
                LexicalContent::LeftParenthesis => {
                    compiler.add_error(&item.location, "LeftParenthesis".to_owned())
                }
                LexicalContent::RightParenthesis => {
                    compiler.add_error(&item.location, "RightParenthesis".to_owned())
                }
                LexicalContent::LeftBracket => {
                    compiler.add_error(&item.location, "LeftBracket".to_owned())
                }
                LexicalContent::RightBracket => {
                    compiler.add_error(&item.location, "RightBracket".to_owned())
                }
                LexicalContent::LeftBrace => {
                    compiler.add_error(&item.location, "LeftBrace".to_owned())
                }
                LexicalContent::RightBrace => {
                    compiler.add_error(&item.location, "RightBrace".to_owned())
                }
                LexicalContent::UnicodeCharacter(character) => {
                    compiler.add_error(&item.location, format!("UnicodeCharacter <{}>", character))
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
}
