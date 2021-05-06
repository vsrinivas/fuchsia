// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Documentation library. Used by fidldoc.
//!
//! This library provide checks for the documentation.

mod lexer;
mod parser;
mod source;
mod utils;

use std::cmp;
use std::rc::Rc;

/// Defines a documentation compiler which is used to check some documentation.
pub struct DocCompiler {
    /// All the errors encountered during the documentation check (empty if no error).
    pub errors: String,
}

impl DocCompiler {
    /// Create a new documentation compiler.
    pub fn new() -> DocCompiler {
        DocCompiler { errors: String::new() }
    }

    /// Parse some documentation. This methods checks the given documentation.
    ///
    /// This method can be called several times to check more than one documentation.
    /// - file_name:  the name of the file which contains the documentation.
    /// - line: the position in the file (line) of the first character of the documentation.
    /// - column: the position in the file (column) of the first character of the documentation.
    /// - doc: the text of the documentation.
    /// Returns true in case of a successful check.
    pub fn parse_doc(&mut self, file_name: String, line: u32, column: u32, doc: String) -> bool {
        let source = Rc::new(source::Source::new(file_name, line, column, doc));
        if source.text.is_empty() {
            let location = source::Location { source, start: 0, end: 1 };
            self.add_error(&location, "Documentation is empty".to_owned());
        } else {
            if let Some(items) = lexer::reduce_lexems(self, &source) {
                if let Some(_text) = parser::parse_text(self, items) {
                    // The next stage is not implemented yet. Just return true.
                    return true;
                }
            }
        }
        false
    }

    fn add_error(&mut self, location: &source::Location, message: String) {
        let mut line = 0;
        let mut column = 0;
        let mut start_of_line = 0;
        let mut current = location.source.text.char_indices();

        // Computes the line and column of start relative to the beginning of the text.
        // Remembers the index of the first character of the line.
        let mut found_end_of_line = false;
        while let Some((index, character)) = current.next() {
            if index == location.start {
                if character == '\n' {
                    found_end_of_line = true;
                }
                break;
            }
            if character == '\n' {
                line += 1;
                column = 0;
                start_of_line = index + 1;
            } else {
                column += 1;
            }
        }

        // Computes the index of the last character of the line.
        let end_of_line = if found_end_of_line {
            location.start
        } else {
            loop {
                match current.next() {
                    Some((index, character)) => {
                        if character == '\n' {
                            break index;
                        }
                    }
                    None => {
                        break location.source.text.len();
                    }
                }
            }
        };

        // Displays the full line where we have an error.
        self.errors.push_str(&location.source.text[start_of_line..end_of_line]);
        self.errors.push('\n');

        // Displays carets bellow the error.
        self.errors.push_str(&" ".repeat(column));
        let end = cmp::min(location.end, end_of_line);
        // When the location is after the last text character, we still want to display one caret.
        let end = cmp::max(end, location.start + 1);
        self.errors.push_str(&"^".repeat(end - location.start));
        self.errors.push('\n');

        // Displays the error.
        self.errors.push_str(&*format!(
            "{}: {}:{}: {}\n",
            location.source.file_name,
            location.source.line + line,
            location.source.column + (column as u32),
            message
        ));
    }
}

#[cfg(test)]
mod test {
    use crate::source::Location;
    use crate::source::Source;
    use crate::DocCompiler;
    use std::rc::Rc;

    #[test]
    fn start_of_line_error() {
        let mut compiler = DocCompiler::new();
        let source = Rc::new(Source::new(
            "sdk/foo/foo.fidl".to_owned(),
            10,
            4,
            "Some documentation.\nSecond line.".to_owned(),
        ));
        let location = Location { source: source, start: 0, end: 4 };
        compiler.add_error(&location, "Error here".to_owned());
        assert_eq!(
            compiler.errors,
            "\
Some documentation.
^^^^
sdk/foo/foo.fidl: 10:4: Error here
"
        );
    }

    #[test]
    fn end_of_line_error() {
        let mut compiler = DocCompiler::new();
        let source = Rc::new(Source::new(
            "sdk/foo/foo.fidl".to_owned(),
            10,
            4,
            "Some documentation.\nSecond line.".to_owned(),
        ));
        let location = Location { source: source, start: 5, end: 18 };
        compiler.add_error(&location, "Error here".to_owned());
        assert_eq!(
            compiler.errors,
            "\
Some documentation.
     ^^^^^^^^^^^^^
sdk/foo/foo.fidl: 10:9: Error here
"
        );
    }

    #[test]
    fn last_line_error() {
        let mut compiler = DocCompiler::new();
        let source = Rc::new(Source::new(
            "sdk/foo/foo.fidl".to_owned(),
            10,
            4,
            "Some documentation.\nSecond line.".to_owned(),
        ));
        let location = Location { source: source, start: 20, end: 26 };
        compiler.add_error(&location, "Error here".to_owned());
        assert_eq!(
            compiler.errors,
            "\
Second line.
^^^^^^
sdk/foo/foo.fidl: 11:4: Error here
"
        );
    }

    #[test]
    // For a multi line error, we only display up to the end of the first line.
    fn multi_line_error() {
        let mut compiler = DocCompiler::new();
        let source = Rc::new(Source::new(
            "sdk/foo/foo.fidl".to_owned(),
            10,
            4,
            "Some documentation.\nSecond line.".to_owned(),
        ));
        let location = Location { source: source, start: 5, end: 26 };
        compiler.add_error(&location, "Error here".to_owned());
        assert_eq!(
            compiler.errors,
            "\
Some documentation.
     ^^^^^^^^^^^^^^
sdk/foo/foo.fidl: 10:9: Error here
"
        );
    }

    #[test]
    fn empty_test() {
        let mut compiler = DocCompiler::new();
        assert!(!compiler.parse_doc("sdk/foo/foo.fidl".to_owned(), 10, 4, "".to_owned()));
        assert_eq!(compiler.errors, "\n^\nsdk/foo/foo.fidl: 10:4: Documentation is empty\n");
    }

    #[test]
    fn not_empty_test() {
        let mut compiler = DocCompiler::new();
        assert!(compiler.parse_doc(
            "sdk/foo/foo.fidl".to_owned(),
            10,
            4,
            "Some documentation.\n".to_owned()
        ));
        assert!(compiler.errors.is_empty());
    }
}
