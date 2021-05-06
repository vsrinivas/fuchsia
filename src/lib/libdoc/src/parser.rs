// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Defines the parser for the Documentation Compiler.
//! It uses all the lexical items and creates a structured text.

use crate::lexer::LexicalContent;
use crate::lexer::LexicalItem;
use crate::source::Location;
use crate::DocCompiler;
use std::rc::Rc;

/// Defines parsed text.
///
/// A text holds paragraphs.
pub struct Text {
    pub paragraphs: Vec<Paragraph>,
}

impl Text {
    pub fn new() -> Self {
        Self { paragraphs: Vec::new() }
    }
}

/// Defines a paragraph.
///
/// A paragraph holds sentences. Paragraphs are separated by a blank line.
pub struct Paragraph {
    pub location: Location,
    pub sentences: Vec<Sentence>,
}

impl Paragraph {
    pub fn new(location: &Location) -> Self {
        Self {
            location: Location {
                source: Rc::clone(&location.source),
                start: location.start,
                end: location.start,
            },
            sentences: Vec::new(),
        }
    }
}

/// A sentence. That is a list of words with evetually some ponctuation like comma or semilon but
/// not a colon. A sentence is ended with a period.
///
/// A sentence holds lexical items. The blank spaces and the period at the end of sentence are not
/// part of the lexical items (they are part of the sentence but not of the representation of the
/// sentence).
pub struct Sentence {
    pub location: Location,
    pub items: Vec<LexicalItem>,
}

impl Sentence {
    pub fn new(location: &Location) -> Self {
        Self {
            location: Location {
                source: Rc::clone(&location.source),
                start: location.start,
                end: location.end,
            },
            items: Vec::new(),
        }
    }
}

/// Uses a list of lexical items created by reduce_lexems and creates a structured text.
///
/// Returns the structured text (a tree).
pub fn parse_text(compiler: &mut DocCompiler, items: Vec<LexicalItem>) -> Option<Text> {
    let mut text = Text::new();
    let mut item: usize = 0;
    let mut ok = true;
    // Loops until the end of the input is reached.
    loop {
        match &items[item].content {
            LexicalContent::EndOfInput => {
                return if ok { Some(text) } else { None };
            }
            _ => {
                if let Some(paragraph) = parse_paragraph(compiler, &items, &mut item) {
                    text.paragraphs.push(paragraph);
                } else {
                    ok = false;
                }
            }
        }
    }
}

/// Generates a paragraph from a list of lexcical items.
///
/// Paragraphs are separated by one blank line.
pub fn parse_paragraph(
    compiler: &mut DocCompiler,
    items: &Vec<LexicalItem>,
    item: &mut usize,
) -> Option<Paragraph> {
    let mut paragraph = Paragraph::new(&items[*item].location);
    let mut ok = true;
    // Loops until the end of the input or a blank line are found.
    loop {
        match &items[*item].content {
            LexicalContent::EndOfInput => {
                if ok {
                    paragraph.location.end = items[*item].location.start;
                    return Some(paragraph);
                }
                return None;
            }
            LexicalContent::Spaces(count) => {
                if *count > 2 {
                    compiler.add_error(
                        &items[*item].location,
                        "Only one or two spaces allowed between sentences".to_owned(),
                    );
                    ok = false;
                }
                *item = *item + 1;
            }
            LexicalContent::NewLines(count) => {
                if *count > 1 {
                    if *count > 2 {
                        compiler.add_error(
                            &items[*item].location,
                            "Only one blank line allowed between paragraphs".to_owned(),
                        );
                        ok = false;
                    }
                    *item = *item + 1;
                    if ok {
                        paragraph.location.end = items[*item].location.start;
                        return Some(paragraph);
                    }
                    return None;
                }
                *item = *item + 1;
            }
            _ => {
                if let Some(sentence) = parse_sentence(compiler, &items, item) {
                    paragraph.sentences.push(sentence);
                } else {
                    ok = false;
                }
            }
        }
    }
}

/// Parses a sentence within a paragraph.
pub fn parse_sentence(
    compiler: &mut DocCompiler,
    items: &Vec<LexicalItem>,
    item: &mut usize,
) -> Option<Sentence> {
    let mut sentence = Sentence::new(&items[*item].location);
    let mut ok = true;
    // Loops forever (the exit is done when the end of the input is reached).
    loop {
        match &items[*item].content {
            LexicalContent::EndOfInput => {
                compiler.add_error(&items[*item].location, "Unterminated sentence".to_owned());
                return None;
            }
            LexicalContent::Spaces(count) => {
                if *count > 1 {
                    compiler.add_error(
                        &items[*item].location,
                        "Only once space allowed between words".to_owned(),
                    );
                    ok = false;
                }
            }
            LexicalContent::NewLines(count) => {
                if *count > 1 {
                    compiler.add_error(
                        &items[*item].location,
                        "Blank line in the middle of a sentence".to_owned(),
                    );
                    ok = false;
                }
            }
            LexicalContent::EndOfSentence(_character) => {
                sentence.location.end = items[*item].location.end;
                *item = *item + 1;
                return if ok { Some(sentence) } else { None };
            }
            _ => sentence.items.push(items[*item].clone()),
        }
        *item = *item + 1;
    }
}

#[cfg(test)]
mod test {
    use crate::lexer::reduce_lexems;
    use crate::parser::parse_text;
    use crate::parser::Text;
    use crate::source::Source;
    use crate::utils::test::lexical_items_to_errors;
    use crate::DocCompiler;
    use std::rc::Rc;

    /// Convert the text to errors be able to check it.
    fn text_to_errors(compiler: &mut DocCompiler, text: &Text) {
        for paragraph in text.paragraphs.iter() {
            compiler.add_error(&paragraph.location, "Paragraph".to_owned());
            for sentence in paragraph.sentences.iter() {
                compiler.add_error(&sentence.location, "Sentence".to_owned());
                lexical_items_to_errors(compiler, &sentence.items, /*with_spaces=*/ true);
            }
        }
    }

    #[test]
    fn parser_single_sentence() {
        let mut compiler = DocCompiler::new();
        let source = Rc::new(Source::new(
            "sdk/foo/foo.fidl".to_owned(),
            10,
            4,
            "Some documentation.".to_owned(),
        ));
        let items = reduce_lexems(&mut compiler, &source);
        assert!(!items.is_none());
        let text = parse_text(&mut compiler, items.unwrap());
        assert!(!text.is_none());
        text_to_errors(&mut compiler, &text.unwrap());
        assert_eq!(
            compiler.errors,
            "\
Some documentation.
^^^^^^^^^^^^^^^^^^^
sdk/foo/foo.fidl: 10:4: Paragraph
Some documentation.
^^^^^^^^^^^^^^^^^^^
sdk/foo/foo.fidl: 10:4: Sentence
Some documentation.
^^^^
sdk/foo/foo.fidl: 10:4: Name <Some>
Some documentation.
     ^^^^^^^^^^^^^
sdk/foo/foo.fidl: 10:9: Name <documentation>
"
        );
    }

    #[test]
    fn parser_sentence_with_new_line() {
        let mut compiler = DocCompiler::new();
        let source =
            Rc::new(Source::new("sdk/foo/foo.fidl".to_owned(), 10, 4, "Some\ndoc.".to_owned()));
        let items = reduce_lexems(&mut compiler, &source);
        assert!(!items.is_none());
        let text = parse_text(&mut compiler, items.unwrap());
        assert!(!text.is_none());
        text_to_errors(&mut compiler, &text.unwrap());
        assert_eq!(
            compiler.errors,
            "\
Some
^^^^
sdk/foo/foo.fidl: 10:4: Paragraph
Some
^^^^
sdk/foo/foo.fidl: 10:4: Sentence
Some
^^^^
sdk/foo/foo.fidl: 10:4: Name <Some>
doc.
^^^
sdk/foo/foo.fidl: 11:4: Name <doc>
"
        );
    }

    #[test]
    fn parser_two_sentences() {
        let mut compiler = DocCompiler::new();
        let source = Rc::new(Source::new(
            "sdk/foo/foo.fidl".to_owned(),
            10,
            4,
            "Some doc. Other doc.".to_owned(),
        ));
        let items = reduce_lexems(&mut compiler, &source);
        assert!(!items.is_none());
        let text = parse_text(&mut compiler, items.unwrap());
        assert!(!text.is_none());
        text_to_errors(&mut compiler, &text.unwrap());
        assert_eq!(
            compiler.errors,
            "\
Some doc. Other doc.
^^^^^^^^^^^^^^^^^^^^
sdk/foo/foo.fidl: 10:4: Paragraph
Some doc. Other doc.
^^^^^^^^^
sdk/foo/foo.fidl: 10:4: Sentence
Some doc. Other doc.
^^^^
sdk/foo/foo.fidl: 10:4: Name <Some>
Some doc. Other doc.
     ^^^
sdk/foo/foo.fidl: 10:9: Name <doc>
Some doc. Other doc.
          ^^^^^^^^^^
sdk/foo/foo.fidl: 10:14: Sentence
Some doc. Other doc.
          ^^^^^
sdk/foo/foo.fidl: 10:14: Name <Other>
Some doc. Other doc.
                ^^^
sdk/foo/foo.fidl: 10:20: Name <doc>
"
        );
    }

    #[test]
    fn parser_two_paragraphs() {
        let mut compiler = DocCompiler::new();
        let source = Rc::new(Source::new(
            "sdk/foo/foo.fidl".to_owned(),
            10,
            4,
            "Some doc.\n\nOther doc.\n".to_owned(),
        ));
        let items = reduce_lexems(&mut compiler, &source);
        assert!(!items.is_none());
        let text = parse_text(&mut compiler, items.unwrap());
        assert!(!text.is_none());
        text_to_errors(&mut compiler, &text.unwrap());
        assert_eq!(
            compiler.errors,
            "\
Some doc.
^^^^^^^^^
sdk/foo/foo.fidl: 10:4: Paragraph
Some doc.
^^^^^^^^^
sdk/foo/foo.fidl: 10:4: Sentence
Some doc.
^^^^
sdk/foo/foo.fidl: 10:4: Name <Some>
Some doc.
     ^^^
sdk/foo/foo.fidl: 10:9: Name <doc>
Other doc.
^^^^^^^^^^
sdk/foo/foo.fidl: 12:4: Paragraph
Other doc.
^^^^^^^^^^
sdk/foo/foo.fidl: 12:4: Sentence
Other doc.
^^^^^
sdk/foo/foo.fidl: 12:4: Name <Other>
Other doc.
      ^^^
sdk/foo/foo.fidl: 12:10: Name <doc>
"
        );
    }

    #[test]
    fn parser_too_many_lines_between_paragraphs() {
        let mut compiler = DocCompiler::new();
        let source = Rc::new(Source::new(
            "sdk/foo/foo.fidl".to_owned(),
            10,
            4,
            "Some doc.\n\n\nOther doc.".to_owned(),
        ));
        let items = reduce_lexems(&mut compiler, &source);
        assert!(!items.is_none());
        let text = parse_text(&mut compiler, items.unwrap());
        assert!(text.is_none());
        assert_eq!(
            compiler.errors,
            "\
Some doc.
         ^
sdk/foo/foo.fidl: 10:13: Only one blank line allowed between paragraphs
"
        );
    }

    #[test]
    fn parser_too_many_spaces_between_sentences() {
        let mut compiler = DocCompiler::new();
        let source = Rc::new(Source::new(
            "sdk/foo/foo.fidl".to_owned(),
            10,
            4,
            "Some doc.   Other doc.".to_owned(),
        ));
        let items = reduce_lexems(&mut compiler, &source);
        assert!(!items.is_none());
        let text = parse_text(&mut compiler, items.unwrap());
        assert!(text.is_none());
        assert_eq!(
            compiler.errors,
            "\
Some doc.   Other doc.
         ^^^
sdk/foo/foo.fidl: 10:13: Only one or two spaces allowed between sentences
"
        );
    }

    #[test]
    fn parser_unterminated_sentence() {
        let mut compiler = DocCompiler::new();
        let source =
            Rc::new(Source::new("sdk/foo/foo.fidl".to_owned(), 10, 4, "Some doc".to_owned()));
        let items = reduce_lexems(&mut compiler, &source);
        assert!(!items.is_none());
        let text = parse_text(&mut compiler, items.unwrap());
        assert!(text.is_none());
        assert_eq!(
            compiler.errors,
            "\
Some doc
        ^
sdk/foo/foo.fidl: 10:12: Unterminated sentence
"
        );
    }

    #[test]
    fn parser_too_many_spaces_within_sentence() {
        let mut compiler = DocCompiler::new();
        let source =
            Rc::new(Source::new("sdk/foo/foo.fidl".to_owned(), 10, 4, "Some  doc.".to_owned()));
        let items = reduce_lexems(&mut compiler, &source);
        assert!(!items.is_none());
        let text = parse_text(&mut compiler, items.unwrap());
        assert!(text.is_none());
        assert_eq!(
            compiler.errors,
            "\
Some  doc.
    ^^
sdk/foo/foo.fidl: 10:8: Only once space allowed between words
"
        );
    }

    #[test]
    fn parser_blank_line_within_sentence() {
        let mut compiler = DocCompiler::new();
        let source =
            Rc::new(Source::new("sdk/foo/foo.fidl".to_owned(), 10, 4, "Some\n\ndoc.".to_owned()));
        let items = reduce_lexems(&mut compiler, &source);
        assert!(!items.is_none());
        let text = parse_text(&mut compiler, items.unwrap());
        assert!(text.is_none());
        assert_eq!(
            compiler.errors,
            "\
Some
    ^
sdk/foo/foo.fidl: 10:8: Blank line in the middle of a sentence
"
        );
    }
}
