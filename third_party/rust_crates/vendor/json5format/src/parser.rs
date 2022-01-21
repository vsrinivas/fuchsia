// Copyright (c) 2020 Google LLC All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#![deny(missing_docs)]
use {
    crate::{content::*, error::*},
    lazy_static::lazy_static,
    regex::{CaptureLocations, Match, Regex},
    std::cell::RefCell,
    std::rc::Rc,
};

/// All of the regular expressions in this module consume from the start of the remaining characters
/// in the input buffer. To make it more clear that the Regex instances must start with "^", this
/// function prepends the "^" to the start of the rest of the regex string, and all Regex
/// declarations start with this function.
fn from_start(regex: &str) -> String {
    "^".to_owned() + regex
}

/// Wraps a regex pattern to match the string *only* if it matches the entire string.
fn exact_match(regex: &str) -> String {
    "^".to_owned() + regex + "$"
}

lazy_static! {

    /// Any unicode whitespace except newline.
    static ref WHITESPACE_PATTERN: &'static str = r#"([\s&&[^\n]]+)"#;
    static ref WHITESPACE: usize = 1;

    /// Match a newline (except newlines in multiline strings and block comments).
    static ref NEWLINE_PATTERN: &'static str = r#"(\n)"#;
    static ref NEWLINE: usize = 2;

    /// Match two slashes before capturing the line comment. Additional slashes and leading spaces
    /// are considered part of the content, so they will be accurately restored by the formatter.
    static ref LINE_COMMENT_SLASHES_PATTERN: &'static str = r#"(//)"#;
    static ref LINE_COMMENT_SLASHES: usize = 3;

    /// Match the start of a block comment.
    static ref OPEN_BLOCK_COMMENT_PATTERN: &'static str = r#"(/\*)"#;
    static ref OPEN_BLOCK_COMMENT: usize = 4;

    /// Any non-string primitive (Number, Boolean, 'null').
    static ref NON_STRING_PRIMITIVE_PATTERN: &'static str =
        r#"((?x) # ignore whitespace and allow '#' comments

            # Capture null, true, or false (lowercase only, as in the ECMAScript keywords).
            # End with a word boundary ('\b' marker) to ensure the pattern does not match if
            # it is followed by a word ('\w') character; for example, 'nullify' is a valid
            # identifier (depending on the context) and must not match the 'null' value.

            (?:(?:null|true|false)\b)|

            # Capture all number formats. Every variant is allowed an optional '-' or '+' prefix.

            (?:[-+]?(?:

                # All of the following variants end in a word character. Use '\b' to prevent
                # matching numbers immediately followed by another word character, for example,
                # 'NaNo', 'Infinity_', or '0xadef1234ghi'.

                (?:(?:
                    NaN|
                    Infinity|
                    (?:0[xX][0-9a-fA-F]+)|     # hexadecimal notation
                    (?:[0-9]+[eE][+-]?[0-9]+)| # exponent notation
                    (?:[0-9]*\.[0-9]+)         # decimal notation
                )\b)|

                # Capture integers, with an optional trailing decimal point.
                # If the value ends in a digit (no trailing decimal point), apply `\b` to prevent
                # matching integers immediatly followed by a word character (for example, 1200PDT).
                # But if the integer has a trailing decimal, the '\b' does not apply. (Since '.' is
                # not itself a '\w' word character, the '\b' would have the opposite affect,
                # matching only if the next character is a word character, unless there is no next
                # character.)

                (?:
                    [0-9]+(?:\.|\b)
                )
            ))
        )"#;
    static ref NON_STRING_PRIMITIVE: usize = 5;

    /// Property name without quotes.
    static ref UNQUOTED_PROPERTY_NAME_PATTERN: &'static str = r#"[\$\w&&[^\d]][\$\w]*"#;
    static ref UNQUOTED_PROPERTY_NAME_REGEX: Regex =
        Regex::new(&exact_match(&*UNQUOTED_PROPERTY_NAME_PATTERN)).unwrap();

    static ref UNQUOTED_PROPERTY_NAME_AND_COLON_PATTERN_STRING: String =
        r#"(?:("#.to_owned() + *UNQUOTED_PROPERTY_NAME_PATTERN + r#")[\s&&[^\n]]*:)"#;
    static ref UNQUOTED_PROPERTY_NAME_AND_COLON_PATTERN: &'static str =
        &UNQUOTED_PROPERTY_NAME_AND_COLON_PATTERN_STRING;
    static ref UNQUOTED_PROPERTY_NAME_AND_COLON: usize = 6;

    /// Initial quote for a single or double quote string.
    static ref OPEN_QUOTE_PATTERN: &'static str = r#"(["'])"#;
    static ref OPEN_QUOTE: usize = 7;

    /// An opening or closing curly brace or square brace.
    static ref BRACE_PATTERN: &'static str = r#"([{}\[\]])"#;
    static ref BRACE: usize = 8;

    /// Match a comma, separating object properties and array items
    static ref COMMA_PATTERN: &'static str = r#"(,)"#;
    static ref COMMA: usize = 9;

    /// Capture any of the above tokens. These regular expressions are designed for an exclusive
    /// match, so only one of the tokens should match a valid JSON 5 document fragement, when
    /// applied.
    static ref NEXT_TOKEN: Regex = Regex::new(
        &from_start(&(r#"(?:"#.to_owned()
        + &vec![
            *WHITESPACE_PATTERN,
            *NEWLINE_PATTERN,
            *LINE_COMMENT_SLASHES_PATTERN,
            *OPEN_BLOCK_COMMENT_PATTERN,
            *NON_STRING_PRIMITIVE_PATTERN,
            *UNQUOTED_PROPERTY_NAME_AND_COLON_PATTERN,
            *OPEN_QUOTE_PATTERN,
            *BRACE_PATTERN,
            *COMMA_PATTERN,
        ].join("|")
        + r#")"#))
    ).unwrap();

    /// Capture the contents of a line comment.
    static ref LINE_COMMENT: Regex = Regex::new(
        &from_start(r#"([^\n]*)"#)
    ).unwrap();

    /// Capture the contents of a block comment.
    static ref BLOCK_COMMENT: Regex = Regex::new(
        &from_start(r#"((?:.|\n)*?)\*/"#)
    ).unwrap();

    /// Capture the string, without quotes.
    static ref SINGLE_QUOTED: Regex = Regex::new(
        &from_start(r#"((?:(?:\\\\)|(?:\\')|(?:\\\n)|(?:[^'\n]))*)(?:')"#)
    ).unwrap();

    /// Capture the string, without quotes.
    static ref DOUBLE_QUOTED: Regex = Regex::new(
        &from_start(r#"((?:(?:\\\\)|(?:\\")|(?:\\\n)|(?:[^"\n]))*)(?:")"#)
    ).unwrap();

    /// Quoted property names are captured using the same regex as quoted string primitives, and
    /// unlike `UNQUOTED_PROPERTY_NAME_AND_COLON`, the property name separator (colon with optional
    /// whitespace) is not automatically consumed. Use this regex to consume the separator after
    /// encountering a quoted string in the property name position.
    static ref COLON: Regex = Regex::new(
        &from_start(r#"([\s&&[^\n]]*:)"#)
    ).unwrap();
}

fn matches_unquoted_property_name(strval: &str) -> bool {
    const KEYWORDS: &'static [&'static str] = &["true", "false", "null"];
    UNQUOTED_PROPERTY_NAME_REGEX.is_match(strval) && !KEYWORDS.contains(&strval)
}

struct Capturer {
    regex: &'static Regex,
    overall_match: Option<String>,
    locations: CaptureLocations,
}

impl Capturer {
    fn new(regex: &'static Regex) -> Self {
        Self { regex, overall_match: None, locations: regex.capture_locations() }
    }

    fn capture<'a>(&mut self, text: &'a str) -> Option<Match<'a>> {
        let captures = self.regex.captures_read(&mut self.locations, text);
        if let Some(captures) = &captures {
            self.overall_match = Some(text[0..captures.end()].to_string());
        } else {
            self.overall_match = None;
        }
        captures
    }

    fn overall_match<'a>(&'a self) -> Option<&'a str> {
        self.overall_match.as_deref()
    }

    fn captured<'a>(&'a self, i: usize) -> Option<&'a str> {
        if let (Some(overall_match), Some((start, end))) =
            (&self.overall_match, self.locations.get(i))
        {
            Some(&overall_match[start..end])
        } else {
            None
        }
    }
}

/// This internal struct holds the information needed to print a
/// contextually-relevant portion of the line (if not the entire line) where a
/// parser error was caught, the first character of the error on that line, and
/// the number of characters from that initial character index (1 or more) to
/// highlight as being part of the error.
struct ParserErrorContext {
    /// The error line to be printed with a parser error.
    line: String,

    /// The starting character of the error (zero-based index).
    indicator_start: usize,

    /// The number of characters to highlight, including the character at the
    /// `indicator_start` (at least 1).
    indicator_len: usize,
}

impl ParserErrorContext {
    fn new(line: String, indicator_start: usize, indicator_len: usize) -> Self {
        assert!(indicator_len >= 1);
        Self { line, indicator_start, indicator_len }
    }

    fn line(&self) -> &str {
        &self.line
    }

    fn indicator(&self) -> String {
        let mut line = " ".repeat(self.indicator_start) + "^";
        if self.indicator_len > 1 {
            line += &"~".repeat(self.indicator_len - 1);
        }
        line
    }
}

pub(crate) struct Parser<'parser> {
    /// The remaining text in the input buffer since the last capture.
    remaining: &'parser str,

    /// The input filename, if any.
    filename: &'parser Option<String>,

    /// The text of the current line being parsed.
    current_line: &'parser str,

    /// The text of the next line to be parsed after parsing the last capture.
    next_line: &'parser str,

    /// The current line number (from 1) while parsing the input buffer.
    line_number: usize,

    /// The current column number (from 1) while parsing the input buffer.
    column_number: usize,

    /// The line number of the next token to be parsed.
    next_line_number: usize,

    /// The column number of the next token to be parsed.
    next_column_number: usize,

    /// The top of the stack is the current Object or Array whose content is being parsed.
    /// Tne next item in the stack is the Object or Array that contains the current one,
    /// and so on.
    scope_stack: Vec<Rc<RefCell<Value>>>,

    /// To avoid accidentally overflowing the program stack, limit the number of
    /// nested scopes and generate an error if it is exceeded.
    nesting_limit: usize,

    /// Captures a colon token when expected.
    colon_capturer: Capturer,
}

impl<'parser> Parser<'parser> {
    /// The default limit of nested scopes when parsing a JSON5 document.
    pub const DEFAULT_NESTING_LIMIT: usize = 1000;

    pub fn new(filename: &'parser Option<String>) -> Self {
        let remaining = "";
        let current_line = &remaining;
        Self {
            remaining,
            filename,
            current_line,
            next_line: current_line,
            line_number: 1,
            column_number: 1,
            next_line_number: 1,
            next_column_number: 1,
            scope_stack: Vec::default(),
            nesting_limit: Self::DEFAULT_NESTING_LIMIT,
            colon_capturer: Capturer::new(&COLON),
        }
    }

    /// To avoid accidentally overflowing the program stack, there is a mutable
    /// limit on the number of nested scopes allowed. If this limit is exceeded
    /// while parsing a document, a parser error is generated.
    pub fn set_nesting_limit(&mut self, new_limit: usize) {
        self.nesting_limit = new_limit;
    }

    fn current_scope(&self) -> Rc<RefCell<Value>> {
        assert!(self.scope_stack.len() > 0);
        self.scope_stack.last().unwrap().clone()
    }

    fn with_container<F, T>(&self, f: F) -> Result<T, Error>
    where
        F: FnOnce(&mut dyn Container) -> Result<T, Error>,
    {
        match &mut *self.current_scope().borrow_mut() {
            Value::Array { val, .. } => f(val),
            Value::Object { val, .. } => f(val),
            unexpected => Err(Error::internal(
                self.location(),
                format!(
                    "Current scope should be an Array or Object, but scope was {:?}",
                    unexpected
                ),
            )),
        }
    }

    fn with_array<F, T>(&self, f: F) -> Result<T, Error>
    where
        F: FnOnce(&mut Array) -> Result<T, Error>,
    {
        match &mut *self.current_scope().borrow_mut() {
            Value::Array { val, .. } => f(val),
            unexpected => Err(self.error(format!(
                "Invalid Array token found while parsing an {:?} (mismatched braces?)",
                unexpected
            ))),
        }
    }

    fn with_object<F, T>(&self, f: F) -> Result<T, Error>
    where
        F: FnOnce(&mut Object) -> Result<T, Error>,
    {
        match &mut *self.current_scope().borrow_mut() {
            Value::Object { val, .. } => f(val),
            unexpected => Err(self.error(format!(
                "Invalid Object token found while parsing an {:?} (mismatched braces?)",
                unexpected
            ))),
        }
    }

    fn is_in_array(&self) -> bool {
        (*self.current_scope().borrow()).is_array()
    }

    fn is_in_object(&self) -> bool {
        !self.is_in_array()
    }

    fn add_value(&mut self, value: Value) -> Result<(), Error> {
        let is_container = value.is_object() || value.is_array();
        let value_ref = Rc::new(RefCell::new(value));
        self.with_container(|container| container.add_value(value_ref.clone(), self))?;
        if is_container {
            self.scope_stack.push(value_ref.clone());
            if self.scope_stack.len() > self.nesting_limit {
                return Err(self.error(format!(
                    "The given JSON5 document exceeds the parser's nesting limit of {}",
                    self.nesting_limit
                )));
            }
        }
        Ok(())
    }

    fn on_newline(&mut self) -> Result<(), Error> {
        self.with_container(|container| container.on_newline())
    }

    /// Adds a standalone line comment to the current container, or adds an end-of-line comment
    /// to the current container's current value.
    ///
    /// # Arguments
    ///   * `captured`: the line comment content (including leading spaces)
    ///   * `pending_new_line_comment_block` - If true and the comment is not an
    ///     end-of-line comment, the container should insert a line_comment_break before inserting
    ///     the next line comment. This should only be true if this standalone line comment was
    ///     preceded by one or more standalone line comments and one or more blank lines.
    ///
    /// # Returns
    ///   true if the line comment is standalone, that is, not an end-of-line comment
    fn add_line_comment(
        &self,
        captured: Option<&str>,
        pending_new_line_comment_block: bool,
    ) -> Result<bool, Error> {
        match captured {
            Some(content) => {
                let content = content.trim_end();
                self.with_container(|container| {
                    container.add_line_comment(
                        content,
                        self.column_number,
                        pending_new_line_comment_block,
                    )
                })
            }
            None => Err(Error::internal(
                self.location(),
                "Line comment regex should support empty line comment",
            )),
        }
    }

    fn add_block_comment(&self, captured: Option<&str>) -> Result<(), Error> {
        match captured {
            Some(content) => {
                // `indent_count` subtracts 2 characters for the "/*" prefix on the firt line of
                // the block comment, and 2 spaces on subsequent lines, assuming the line content is
                // meant to be vertically aligned.
                let indent_count = self.column_number - 3;
                let indent = " ".repeat(indent_count);
                if content
                    .lines()
                    .enumerate()
                    .find(|(index, line)| {
                        *index > 0 && !line.starts_with(&indent) && line.trim() != ""
                    })
                    .is_some()
                {
                    self.with_container(|container| {
                        container.add_block_comment(Comment::Block {
                            lines: content.lines().map(|line| line.to_owned()).collect(),
                            align: false,
                        })
                    })
                } else {
                    // All block comment lines are indented at least beyond the "/*", so strip the
                    // indent and re-indent when formatting.
                    let trimmed_lines = content
                        .lines()
                        .enumerate()
                        .map(|(index, line)| {
                            if index == 0 {
                                line
                            } else if line.trim().len() == 0 {
                                ""
                            } else {
                                &line[indent_count..]
                            }
                        })
                        .collect::<Vec<&str>>();
                    self.with_container(|container| {
                        container.add_block_comment(Comment::Block {
                            lines: trimmed_lines.iter().map(|line| line.to_string()).collect(),
                            align: true,
                        })
                    })
                }
            }
            None => return Err(self.error("Block comment started without closing \"*/\"")),
        }
    }

    fn take_pending_comments(&mut self) -> Result<Vec<Comment>, Error> {
        self.with_container(|container| Ok(container.take_pending_comments()))
    }

    /// The given property name was parsed. Once it's value is also parsed, the property will be
    /// added to the current `Object`.
    ///
    /// # Arguments
    ///   * name - the property name, possibly quoted
    fn set_pending_property(&self, name: &str) -> Result<(), Error> {
        self.with_object(|object| object.set_pending_property(name.to_string(), self))
    }

    /// Adds a primitive string value or quoted property name, depending on the current context.
    ///
    /// For property names that meet the requirements for unquoted property names, the unnecessary
    /// quotes are removed; otherwise, the original quotes are retained since the content of the
    /// string may depend on the type of quote. For instance:
    ///
    /// ```json
    ///   'JSON string "with double quotes" wrapped in single quotes'
    /// ```
    ///
    /// As long as the single quotes are restored as-is (and not replaced with double-quotes)
    /// the formatter can restore the original representation of the string without additional
    /// (and perhaps less-readable) escaping of internal quotes.
    fn add_quoted_string(&mut self, quote: &str, captured: Option<&str>) -> Result<(), Error> {
        match captured {
            Some(unquoted) => {
                if self.is_in_object()
                    && !self.with_object(|object| object.has_pending_property())?
                {
                    let captured = self.colon_capturer.capture(self.remaining);
                    if self.consume_if_matched(captured) {
                        if matches_unquoted_property_name(&unquoted) {
                            self.set_pending_property(unquoted)
                        } else {
                            self.set_pending_property(&format!("{}{}{}", quote, &unquoted, quote))
                        }
                    } else {
                        return Err(self.error("Property name separator (:) missing"));
                    }
                } else {
                    let comments = self.take_pending_comments()?;
                    self.add_value(Primitive::new(
                        format!("{}{}{}", quote, &unquoted, quote),
                        comments,
                    ))
                }
            }
            None => return Err(self.error("Unclosed string")),
        }
    }

    fn add_non_string_primitive(&mut self, non_string_primitive: &str) -> Result<(), Error> {
        let comments = self.take_pending_comments()?;
        self.add_value(Primitive::new(non_string_primitive.to_string(), comments))
    }

    fn on_brace(&mut self, brace: &str) -> Result<(), Error> {
        match brace {
            "{" => self.open_object(),
            "}" => self.close_object(),
            "[" => self.open_array(),
            "]" => self.close_array(),
            unexpected => Err(Error::internal(
                self.location(),
                format!("regex returned unexpected brace string: {}", unexpected),
            )),
        }
    }

    fn open_object(&mut self) -> Result<(), Error> {
        let comments = self.take_pending_comments()?;
        self.add_value(Object::new(comments))
    }

    fn exit_scope(&mut self) -> Result<(), Error> {
        self.scope_stack.pop();
        if self.scope_stack.is_empty() {
            Err(self.error("Closing brace without a matching opening brace"))
        } else {
            Ok(())
        }
    }

    fn close_object(&mut self) -> Result<(), Error> {
        self.with_object(|object| object.close(self))?;
        self.exit_scope()
    }

    fn open_array(&mut self) -> Result<(), Error> {
        let comments = self.take_pending_comments()?;
        self.add_value(Array::new(comments))
    }

    fn close_array(&mut self) -> Result<(), Error> {
        self.with_array(|array| array.close(self))?;
        self.exit_scope()
    }

    fn end_value(&self) -> Result<(), Error> {
        self.with_container(|container| container.end_value(self))
    }

    pub fn location(&self) -> Option<Location> {
        Some(Location::new(self.filename.clone(), self.line_number, self.column_number))
    }

    pub fn error(&self, err: impl std::fmt::Display) -> Error {
        const MAX_ERROR_LINE_LEN: usize = 200;
        const MIN_CONTEXT_LEN: usize = 10;
        const ELLIPSIS: &str = "\u{2026}";
        let error_context = self.get_error_context(MAX_ERROR_LINE_LEN, MIN_CONTEXT_LEN, ELLIPSIS);
        Error::parse(
            self.location(),
            format!("{}:\n{}\n{}", err, error_context.line(), error_context.indicator()),
        )
    }

    fn consume_if_matched<'a>(&mut self, matched: Option<Match<'a>>) -> bool {
        self.column_number = self.next_column_number;
        if self.line_number < self.next_line_number {
            self.line_number = self.next_line_number;
            self.current_line = self.next_line;
        }
        if let Some(matched) = matched {
            let matched_and_remaining = &self.remaining[matched.start()..];
            self.remaining = &self.remaining[matched.end()..];

            // If `matched` contains newlines, advance the `next_line` and column, for printing the
            // location of the next syntax element, in error messages, for example.
            let mut some_matched_lines = None;
            for c in matched.as_str().chars() {
                if c == '\n' {
                    let matched_lines = some_matched_lines
                        .get_or_insert_with(|| matched_and_remaining.lines().skip(1));
                    self.next_line = matched_lines.next().unwrap_or(self.current_line);
                    self.next_line_number += 1;
                    self.next_column_number = 1;
                } else {
                    self.next_column_number += 1;
                }
            }
            true
        } else {
            false
        }
    }

    fn capture(&mut self, capturer: &mut Capturer) -> bool {
        self.consume_if_matched(capturer.capture(self.remaining))
    }

    fn consume<'a>(&mut self, capturer: &'a mut Capturer) -> Option<&'a str> {
        if self.capture(capturer) {
            capturer.captured(1)
        } else {
            None
        }
    }

    /// Parse the given document string as a JSON5 document containing Array
    /// elements (with implicit outer braces). Document locations (use in, for
    /// example, error messages), are 1-based and start at line 1, column 1.
    pub fn parse(&mut self, buffer: &'parser str) -> Result<Array, Error> {
        self.parse_from_location(buffer, 1, 1)
    }

    /// Parse the given document string as a JSON5 document containing Array
    /// elements (with implicit outer braces), and use the given 1-based line
    /// and column numbers when referring to document locations.
    pub fn parse_from_location(
        &mut self,
        buffer: &'parser str,
        starting_line_number: usize,
        starting_column_number: usize,
    ) -> Result<Array, Error> {
        self.remaining = buffer;
        self.current_line = &self.remaining;

        assert!(starting_line_number > 0, "document line numbers are 1-based");
        self.next_line_number = starting_line_number;
        self.next_column_number = starting_column_number;

        self.next_line = self.current_line;
        self.line_number = self.next_line_number - 1;
        self.column_number = self.next_column_number - 1;
        self.scope_stack = vec![Rc::new(RefCell::new(Array::new(vec![])))];

        let mut next_token = Capturer::new(&NEXT_TOKEN);
        let mut single_quoted = Capturer::new(&SINGLE_QUOTED);
        let mut double_quoted = Capturer::new(&DOUBLE_QUOTED);
        let mut line_comment = Capturer::new(&LINE_COMMENT);
        let mut block_comment = Capturer::new(&BLOCK_COMMENT);

        // Blocks of adjacent line comments should be kept together as a "line comment block", but
        // adjacent line comment blocks separated by one or more newlines must be maintained as
        // separate blocks.
        //
        // These booleans, along with `reset_line_comment_break_check`, update state information as
        // line comments and blank lines are parsed.
        let mut just_captured_line_comment = false;
        let mut pending_blank_line = false;
        let mut pending_new_line_comment_block = false;

        while self.remaining.len() > 0 {
            // See comment above regarding "line comment blocks".
            let mut reset_line_comment_break_check = true;

            if self.capture(&mut next_token) {
                // Since this has to be done as an if-else-if-... check the most common
                // occurrences first.
                if let Some(_) = next_token.captured(*WHITESPACE) {
                    reset_line_comment_break_check = false;
                    Ok(()) // ignore all whitespace not in a string or comment
                } else if let Some(_) = next_token.captured(*NEWLINE) {
                    reset_line_comment_break_check = false;
                    if just_captured_line_comment {
                        if pending_blank_line {
                            pending_new_line_comment_block = true;
                            pending_blank_line = false;
                        } else if !pending_new_line_comment_block {
                            pending_blank_line = true;
                        }
                    }
                    self.on_newline()
                } else if let Some(_) = next_token.captured(*COMMA) {
                    self.end_value()
                } else if let Some(brace) = next_token.captured(*BRACE) {
                    self.on_brace(&brace)
                } else if let Some(non_string_primitive) =
                    next_token.captured(*NON_STRING_PRIMITIVE)
                {
                    self.add_non_string_primitive(&non_string_primitive)
                } else if let Some(quote) = next_token.captured(*OPEN_QUOTE) {
                    let quoted_string = if quote == "'" {
                        self.consume(&mut single_quoted)
                    } else {
                        self.consume(&mut double_quoted)
                    };
                    self.add_quoted_string(&quote, quoted_string)
                } else if let Some(unquoted_property_name) =
                    next_token.captured(*UNQUOTED_PROPERTY_NAME_AND_COLON)
                {
                    self.set_pending_property(unquoted_property_name)
                } else if let Some(_line_comment_start) = next_token.captured(*LINE_COMMENT_SLASHES)
                {
                    reset_line_comment_break_check = false;
                    pending_blank_line = false;
                    let line_comment = self.consume(&mut line_comment);
                    if self.add_line_comment(line_comment, pending_new_line_comment_block)? {
                        // standalone line comment
                        just_captured_line_comment = true;
                        pending_new_line_comment_block = false;
                    } // else it was an end-of-line comment
                    Ok(())
                } else if let Some(_block_comment_start) = next_token.captured(*OPEN_BLOCK_COMMENT)
                {
                    let block_comment = self.consume(&mut block_comment);
                    self.add_block_comment(block_comment)
                } else {
                    Err(Error::internal(
                        self.location(),
                        format!(
                            "NEXT_TOKEN matched an unexpected capture group: {}",
                            next_token.overall_match().unwrap_or("")
                        ),
                    ))
                }
            } else {
                Err(self.error("Unexpected token"))
            }?;

            if reset_line_comment_break_check {
                just_captured_line_comment = false;
                pending_blank_line = false;
                pending_new_line_comment_block = false;
            }
        }
        self.remaining = "";
        self.close_document()?;

        match Rc::try_unwrap(self.scope_stack.pop().unwrap())
            .map_err(|_| Error::internal(None, "Rc<> for document array could not be unwrapped."))?
            .into_inner()
        {
            Value::Array { val, .. } => Ok(val),
            unexpected => Err(Error::internal(
                self.location(),
                format!("Final scope should be an Array, but scope was {:?}", unexpected),
            )),
        }
    }

    fn close_document(&mut self) -> Result<(), Error> {
        if self.scope_stack.len() == 1 {
            Ok(())
        } else {
            Err(self.error("Mismatched braces in the document"))
        }
    }

    /// Returns the given `current_line` and an `indicator` line: spaces, followed
    /// by a carat (`^`) that points at the given `column_number`, followed by
    /// tilde's (`~`) as long as the error token.
    ///
    /// If the line is longer than a set maximum length, the line is trimmed and
    /// the indicator positions are adjusted.
    fn get_error_context(
        &self,
        max_error_line_len: usize,
        min_context_len: usize,
        ellipsis: &str,
    ) -> ParserErrorContext {
        // `indicator_start` is a 0-based char position
        let indicator_start = self.column_number - 1;

        let error_line_len = self.current_line.chars().count();

        let indicator_len = if self.line_number == self.next_line_number {
            std::cmp::max(
                std::cmp::min(
                    self.next_column_number - self.column_number,
                    error_line_len - indicator_start,
                ),
                1,
            )
        } else {
            1
        };

        if error_line_len <= max_error_line_len {
            ParserErrorContext::new(self.current_line.to_owned(), indicator_start, indicator_len)
        } else {
            trim_error_line_and_indicator(
                self.current_line,
                indicator_start,
                indicator_len,
                error_line_len,
                max_error_line_len,
                min_context_len,
                ellipsis,
            )
        }
    }
}

struct CharRange {
    range: std::ops::Range<usize>,
}

impl CharRange {
    fn new(range: std::ops::Range<usize>) -> Self {
        Self { range }
    }

    fn to_byte_range(self, from_string: &str) -> Option<std::ops::Range<usize>> {
        let char_len = from_string.chars().count();
        let mut some_start_byte =
            if self.range.start == char_len { Some(from_string.len()) } else { None };
        let mut some_end_byte =
            if self.range.end == char_len { Some(from_string.len()) } else { None };
        if let (Some(start_byte), Some(end_byte)) = (some_start_byte, some_end_byte) {
            return Some(start_byte..end_byte);
        }
        for (char_pos, (byte_pos, _char)) in from_string.char_indices().enumerate() {
            if char_pos == self.range.start {
                if let Some(end_byte) = some_end_byte {
                    return Some(byte_pos..end_byte);
                }
                some_start_byte = Some(byte_pos);
            }
            if char_pos == self.range.end {
                if let Some(start_byte) = some_start_byte {
                    return Some(start_byte..byte_pos);
                }
                some_end_byte = Some(byte_pos);
            }
        }
        None
    }
}

fn trim_error_line_and_indicator(
    error_line: &str,
    indicator_start: usize,
    mut indicator_len: usize,
    error_line_len: usize,
    max_error_line_len: usize,
    min_context_len: usize,
    ellipsis: &str,
) -> ParserErrorContext {
    let ellipsis_len = ellipsis.chars().count();

    assert!(max_error_line_len > ellipsis_len);
    assert!(max_error_line_len < error_line_len);
    assert!(indicator_start <= error_line_len);
    assert!(
        indicator_len == 1 || (indicator_start + indicator_len) <= error_line_len,
        "indicator_start={}, indicator_len={}, error_line_len={}\n{}",
        indicator_start,
        indicator_len,
        error_line_len,
        error_line
    );

    indicator_len = std::cmp::min(indicator_len, max_error_line_len);

    let min_right_context_len = std::cmp::max(min_context_len, indicator_len);

    let context_end =
        std::cmp::min(indicator_start + min_right_context_len, error_line_len - ellipsis_len);
    if context_end < max_error_line_len - ellipsis_len {
        let slice_bytes = CharRange::new(0..(max_error_line_len - ellipsis_len))
            .to_byte_range(error_line)
            .expect("char indices should map to String bytes");
        return ParserErrorContext::new(
            error_line[slice_bytes].to_string() + ellipsis,
            indicator_start,
            indicator_len,
        );
    }

    let context_start = indicator_start - std::cmp::min(indicator_start, min_context_len);
    if error_line_len - context_start < max_error_line_len - ellipsis_len {
        let start_char = error_line_len - (max_error_line_len - ellipsis_len);
        let slice_bytes = CharRange::new(start_char..error_line_len)
            .to_byte_range(error_line)
            .expect("char indices should map to String bytes");
        return ParserErrorContext::new(
            ellipsis.to_owned() + &error_line[slice_bytes],
            (indicator_start + ellipsis_len) - start_char,
            indicator_len,
        );
    }

    let margin_chars =
        max_error_line_len - std::cmp::min(max_error_line_len, (ellipsis_len * 2) + indicator_len);
    let right_margin = std::cmp::min(
        error_line_len - std::cmp::min(error_line_len, indicator_start + indicator_len),
        margin_chars / 2,
    );
    let left_margin = margin_chars - right_margin;
    let mut start_char = indicator_start - left_margin;
    let mut end_char =
        std::cmp::min(indicator_start + indicator_len + right_margin, error_line_len);
    let mut start_ellipsis = ellipsis;
    let mut end_ellipsis = ellipsis;
    if start_char == 0 {
        start_ellipsis = "";
        end_char += ellipsis_len;
    } else if end_char == error_line_len {
        end_ellipsis = "";
        start_char -= ellipsis_len;
    }

    let slice_bytes = CharRange::new(start_char..end_char)
        .to_byte_range(error_line)
        .expect("char indices should map to String bytes");
    ParserErrorContext::new(
        start_ellipsis.to_owned() + &error_line[slice_bytes] + end_ellipsis,
        (indicator_start + ellipsis_len) - start_char,
        indicator_len,
    )
}

#[cfg(test)]
mod tests {
    use {super::*, crate::test_error, proptest::prelude::*};

    fn gen_error_line_test(
        error_line: &str,
        pattern: &str,
        max_error_line_len: usize,
        min_context_len: usize,
        ellipsis: &str,
        expected_errorline: &str,
        expected_indicator: &str,
    ) -> Result<(), String> {
        let some_newline = pattern.find("\n");
        let pattern_line1 =
            if let Some(newline) = some_newline { &pattern[0..newline] } else { &pattern };
        assert!(pattern_line1.len() > 0);
        let indicator_start = error_line.find(pattern_line1).expect("pattern not found in line");
        let end = indicator_start + pattern.len();
        let indicator_len = end - indicator_start;
        let error_context = if error_line.chars().count() <= max_error_line_len {
            ParserErrorContext::new(error_line.to_owned(), indicator_start, indicator_len)
        } else {
            trim_error_line_and_indicator(
                error_line,
                indicator_start,
                indicator_len,
                error_line.chars().count(),
                max_error_line_len,
                min_context_len,
                ellipsis,
            )
        };
        let actual_errorline = error_context.line();
        let actual_indicator = error_context.indicator();
        let mut errors = String::new();
        if expected_errorline != actual_errorline {
            println!(
                r#"
expected_errorline: >>>{}<<< (charlen={})
  actual_errorline: >>>{}<<< (charlen={} of {}, min context len={})"#,
                expected_errorline,
                expected_errorline.chars().count(),
                actual_errorline,
                actual_errorline.chars().count(),
                max_error_line_len,
                min_context_len,
            );
            errors.push_str("actual errorline does not match expected");
        } else if expected_indicator != actual_indicator {
            println!(
                r#"
                       {}"#,
                actual_errorline,
            );
        }
        if expected_indicator != actual_indicator {
            if errors.len() > 0 {
                errors.push_str(" and ");
            }
            println!(
                r#"
expected_indicator:    {}
  actual_indicator:    {}"#,
                expected_indicator, actual_indicator,
            );
            errors.push_str("actual indicator does not match expected");
        } else if expected_errorline != actual_errorline {
            println!(
                r#"
                       {}"#,
                actual_indicator,
            );
        }
        if errors.len() > 0 {
            println!("{}", errors);
            Err(errors)
        } else {
            Ok(())
        }
    }

    #[test]
    fn test_error_line1() {
        gen_error_line_test(
            "  good token, bad token;",
            "bad",
            30,
            10,
            " ... ",
            "  good token, bad token;",
            "              ^~~",
        )
        .expect("actual should match expected");
    }

    #[test]
    fn test_error_line2() {
        gen_error_line_test(
            "  good token, bad token;",
            "token;",
            20,
            10,
            " ... ",
            " ... ken, bad token;",
            "              ^~~~~~",
        )
        .expect("actual should match expected");
    }

    #[test]
    fn test_error_line2_short_ellipsis() {
        gen_error_line_test(
            "  good token, bad token;",
            "token;",
            20,
            10,
            "…",
            "…d token, bad token;",
            "              ^~~~~~",
        )
        .expect("actual should match expected");
    }

    #[test]
    fn test_error_line3() {
        gen_error_line_test(
            "A good token, bad token;",
            "bad",
            20,
            10,
            " ... ",
            " ... en, bad to ... ",
            "         ^~~",
        )
        .expect("actual should match expected");
    }

    #[test]
    fn test_error_line3_short_ellipsis() {
        gen_error_line_test(
            "A good token, bad token;",
            "bad",
            20,
            10,
            "…",
            "…d token, bad token;",
            "          ^~~",
        )
        .expect("actual should match expected");
    }

    #[test]
    fn test_error_line3_escaped_unicode_ellipsis() {
        gen_error_line_test(
            "A good token, bad token;",
            "bad",
            20,
            10,
            "\u{2026}",
            "…d token, bad token;",
            "          ^~~",
        )
        .expect("actual should match expected");
    }

    #[test]
    fn test_error_line4() {
        gen_error_line_test(
            "A good token, bad token;",
            "bad",
            10,
            10,
            " ... ",
            " ... bad ... ",
            "     ^~~",
        )
        .expect("actual should match expected");
    }

    #[test]
    fn test_error_line4_short_context() {
        gen_error_line_test(
            "A good token, bad token;",
            "bad",
            10,
            5,
            " ... ",
            " ... bad ... ",
            "     ^~~",
        )
        .expect("actual should match expected");
    }

    #[test]
    fn test_error_line4_long_pattern() {
        gen_error_line_test(
            "A good token, bad token;",
            "bad token",
            10,
            10,
            " ... ",
            " ... bad token ... ",
            "     ^~~~~~~~~",
        )
        .expect("actual should match expected");
    }

    #[test]
    fn test_error_line4_long_pattern_short_context_big_ellipsis() {
        gen_error_line_test(
            "A good token, bad token;",
            "bad token",
            10,
            4,
            " ... ",
            " ... bad token ... ",
            "     ^~~~~~~~~",
        )
        .expect("actual should match expected");
    }

    #[test]
    fn test_error_line4_long_pattern_short_context_short_ellipsis() {
        gen_error_line_test(
            "A good token, bad token;",
            "bad",
            10,
            4,
            "\u{2026}",
            "…n, bad t…",
            "    ^~~",
        )
        .expect("actual should match expected");
    }

    #[test]
    fn test_error_line5() {
        gen_error_line_test(
            r#"[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[{ not a_prop: "value" }]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]"#,
            "a_prop",
            200,
            10,
            " ... ",
            r#"[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[{ not a_prop: "value" }]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]"#,
            r#"                                               ^~~~~~"#,
        ).expect("actual should match expected");
    }

    #[test]
    fn test_error_line6() {
        gen_error_line_test(
            r#"[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[{ not a_prop: "value" }]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]"#,
            "a_prop",
            100,
            10,
            " ... ",
            r#"[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[{ not a_prop: "value" }]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]] ... "#,
            r#"                                               ^~~~~~"#,
        ).expect("actual should match expected");
    }

    #[test]
    fn test_error_line7() {
        gen_error_line_test(
            r#"[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[{ not a_prop: "value" }]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]"#,
            "a_prop",
            100,
            5,
            " ... ",
            r#"[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[{ not a_prop: "value" }]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]] ... "#,
            r#"                                              ^~~~~~"#,
        ).expect("actual should match expected");
    }

    #[test]
    fn test_error_line7_more_braces() {
        gen_error_line_test(
            r#"[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[{ not a_prop: "value" }]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]"#,
            "a_prop",
            100,
            10,
            " ... ",
            r#"[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[{ not a_prop: "value" }]]]]]]]]]]]]]]]]]]]]]]]]]]]] ... "#,
            r#"                                                  ^~~~~~"#,
        ).expect("actual should match expected");
    }

    #[test]
    fn test_error_line8() {
        gen_error_line_test(
            r#"[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[{ not a_prop: "value" }]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]"#,
            "a_prop",
            100,
            10,
            " ... ",
            r#" ... [[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[{ not a_prop: "value" }]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]"#,
            r#"                                           ^~~~~~"#,
        ).expect("actual should match expected");
    }

    #[test]
    fn test_error_line9() {
        gen_error_line_test(
            r#"[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[{ not a_prop: "value" }]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]"#,
            "a_prop",
            100,
            10,
            " ... ",
            r#" ... [[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[{ not a_prop: "value" }]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]] ... "#,
            r#"                                               ^~~~~~"#,
        ).expect("actual should match expected");
    }

    lazy_static! {
        // With `ProptestConfig::failure_persistence` on by default, tests may generate the
        // following warnings:
        //
        //     proptest: Failed to find absolute path of source file...
        //     proptest: FileFailurePersistence::SourceParallel set, but no source file known
        //
        // To suppress these warnings, the following ProptestConfig overrides this behavior:
        static ref NO_PERSIST: ProptestConfig = ProptestConfig {
             failure_persistence: None,
             .. ProptestConfig::default()
        };

        // Overrides the default number of test cases that must pass, from the default of 256.
        static ref EXTRA_CASES_NO_PERSIST: ProptestConfig = ProptestConfig {
             failure_persistence: None,
             cases: 1024,
             .. ProptestConfig::default()
        };
    }

    struct RegexTest<'a> {
        error: Option<&'a str>,
        prefix: &'a str,
        matches: &'a str,
        suffix: &'a str,
        next_regex: Option<&'a Regex>,
        next_prefix: &'a str,
        next_matches: &'a str,
        next_suffix: &'a str,
        trailing: &'a str,
    }

    impl<'a> Default for RegexTest<'a> {
        fn default() -> Self {
            RegexTest {
                error: None,
                prefix: "",
                matches: "",
                suffix: "",
                next_regex: None,
                next_prefix: "",
                next_matches: "",
                next_suffix: "",
                trailing: "",
            }
        }
    }

    /// Validate a regex capture, and optional follow-up capture.
    /// If a test fails, the details of the tests are printed.
    ///
    /// To view the details of a successful tests (such as to validate your proptest patterns are
    /// generating anticipated sample values), run with the following options:
    ///
    ///   $ out/${OUT_SUBDIR}/host_x64/exe.unstripped/json5format_lib_test --show-output <test_name>
    fn try_capture(
        regex: &Regex,
        group_id: Option<usize>,
        test: RegexTest<'_>,
    ) -> Result<String, Error> {
        println!();
        println!("pattern: '{}'", regex.as_str());

        let trailing = test.next_suffix.to_owned() + test.trailing;
        let test_string =
            test.prefix.to_owned() + test.matches + test.suffix + test.next_matches + &trailing;
        println!("capturing from: '{}'", test_string.escape_debug());
        println!(
            "                 {}{}{}{}",
            " ".repeat(test.prefix.len()),
            "^".repeat(test.matches.len()),
            " ".repeat(test.suffix.len()),
            "^".repeat(test.next_matches.len())
        );

        let group_id = group_id.unwrap_or(1);
        println!("expected capture id: '{}'", group_id);

        let capture = regex.captures(&test_string).ok_or_else(|| test_error!("capture failed"))?;
        let overall_match = capture.get(0).ok_or_else(|| test_error!("regex did not match"))?;
        println!(
            "overall match: '{}', length = {}",
            overall_match.as_str().escape_debug(),
            overall_match.end()
        );

        let remaining = &test_string[overall_match.end()..];
        println!("remaining: '{}'", remaining.escape_debug());

        const OVERALL_MATCH: usize = 0;

        let mut capture_ids = vec![];
        for (index, subcapture) in capture.iter().enumerate() {
            if index != OVERALL_MATCH {
                if subcapture.is_some() {
                    capture_ids.push(index);
                }
            }
        }
        println!("capture ids = {:?}", capture_ids);

        let captured_text = capture
            .get(group_id)
            .ok_or_else(|| test_error!(format!("capture group {} did not match", group_id)))?
            .as_str();
        println!("captured: '{}'", captured_text.escape_debug());
        assert_eq!(captured_text, test.matches);
        assert_eq!(capture_ids.len(), 1);
        assert_eq!(remaining, test.next_matches.to_owned() + &trailing);

        match test.next_regex {
            Some(next_regex) => test_capture(
                &*next_regex,
                None,
                RegexTest {
                    prefix: test.next_prefix,
                    matches: test.next_matches,
                    suffix: test.next_suffix,
                    trailing: test.trailing,
                    ..Default::default()
                },
            ),
            None => Ok(captured_text.to_string()),
        }
    }

    fn test_capture(
        regex: &Regex,
        group_id: Option<usize>,
        test: RegexTest<'_>,
    ) -> Result<String, Error> {
        let expected_error_str = test.error.clone();
        match try_capture(regex, group_id, test) {
            Ok(captured) => {
                println!("SUCCESSFUL CAPTURE! ... '{}'", captured);
                Ok(captured)
            }
            Err(actual_error) => match expected_error_str {
                Some(expected_error_str) => match &actual_error {
                    Error::TestFailure(_location, actual_error_str) => {
                        if expected_error_str == actual_error_str {
                            println!("EXPECTED FAILURE (GOOD NEWS)! ... '{}'", actual_error);
                            Ok(format!("{}", actual_error))
                        } else {
                            println!("{}", actual_error);
                            println!("expected: {}", expected_error_str);
                            println!("  actual: {}", actual_error_str);
                            Err(test_error!(
                                "Actual error string did not match expected error string."
                            ))
                        }
                    }
                    _unexpected_error_type => {
                        println!("expected: Test failure: {}", expected_error_str);
                        println!("  actual: {}", actual_error);
                        Err(test_error!(
                            "Actual error type did not match expected test failure type."
                        ))
                    }
                },
                None => Err(actual_error),
            },
        }
    }

    fn test_regex(group_id: usize, test: RegexTest<'_>) -> Result<String, Error> {
        test_capture(&NEXT_TOKEN, Some(group_id), test)
    }

    proptest! {
        #![proptest_config(NO_PERSIST)]
        #[test]
        fn test_whitespace_no_newlines(
            spaces in r#"[\s&&[^\n]]+"#,
            trailing_non_whitespace in r#"[^\s&&[^\n]]*"#,
        ) {
            test_regex(
                *WHITESPACE,
                RegexTest {
                    matches: &spaces,
                    trailing: &trailing_non_whitespace,
                    ..Default::default()
                }
            )
            .unwrap();
        }
    }

    proptest! {
        #![proptest_config(NO_PERSIST)]
        #[test]
        fn test_whitespace_until_newline(
            spaces in r#"[\s&&[^\n]]+"#,
            trailing_non_whitespace in r#"\n[^\s&&[^\n]]*"#,
        ) {
            test_regex(
                *WHITESPACE,
                RegexTest {
                    matches: &spaces,
                    trailing: &trailing_non_whitespace,
                    ..Default::default()
                }
            )
            .unwrap();
        }
    }

    proptest! {
        #![proptest_config(NO_PERSIST)]
        #[test]
        fn test_plain_ascii_whitespace_no_newline(
            spaces in r#"[ \t]+"#,
            trailing_non_whitespace in r#"[^\s&&[^\n]]*"#,
        ) {
            test_regex(
                *WHITESPACE,
                RegexTest {
                    matches: &spaces,
                    trailing: &trailing_non_whitespace,
                    ..Default::default()
                }
            )
            .unwrap();
        }
    }

    proptest! {
        #![proptest_config(NO_PERSIST)]
        #[test]
        fn test_newline(
            newline in r#"\n"#,
            any_chars in r#"\PC*"#,
        ) {
            test_regex(
                *NEWLINE,
                RegexTest { matches: &newline, trailing: &any_chars, ..Default::default() },
            )
            .unwrap();
        }
    }

    proptest! {
        #![proptest_config(NO_PERSIST)]
        #[test]
        fn test_line_comment(
            line_comment_prefix in r#"//"#,
            line_comment_content in r#"(|[^\n][^\n]*)"#,
            more_lines_or_eof in r#"(\n\PC*)?"#,
        ) {
            test_regex(
                *LINE_COMMENT_SLASHES,
                RegexTest {
                    matches: &line_comment_prefix,
                    next_regex: Some(&*LINE_COMMENT),
                    next_matches: &line_comment_content,
                    trailing: &more_lines_or_eof,
                    ..Default::default()
                },
            )
            .unwrap();
        }
    }

    proptest! {
        #![proptest_config(NO_PERSIST)]
        #[test]
        fn test_empty_line_comment(
            line_comment_prefix in r#"//"#,
            more_lines_or_eof in r#"(\n\PC*)?"#,
        ) {
            test_regex(
                *LINE_COMMENT_SLASHES,
                RegexTest {
                    matches: &line_comment_prefix,
                    next_regex: Some(&*LINE_COMMENT),
                    next_matches: "",
                    trailing: &more_lines_or_eof,
                    ..Default::default()
                },
            )
            .unwrap();
        }
    }

    proptest! {
        #![proptest_config(NO_PERSIST)]
        #[test]
        fn test_block_comment(
            block_comment_content in r#"([^*]|([*][^*/]))*"#,
            optional_trailing_content in r#"\PC*"#,
        ) {
            test_regex(
                *OPEN_BLOCK_COMMENT,
                RegexTest {
                    matches: "/*",
                    next_regex: Some(&*BLOCK_COMMENT),
                    next_matches: &block_comment_content,
                    next_suffix: "*/",
                    trailing: &optional_trailing_content,

                    ..Default::default()
                },
            )
            .unwrap();
        }
    }

    proptest! {
        #![proptest_config(NO_PERSIST)]
        #[test]
        fn test_empty_block_comment(
            optional_trailing_content in r#"\PC*"#,
        ) {
            test_regex(
                *OPEN_BLOCK_COMMENT,
                RegexTest {
                    matches: "/*",
                    next_regex: Some(&*BLOCK_COMMENT),
                    next_matches: "",
                    next_suffix: "*/",
                    trailing: &optional_trailing_content,
                    ..Default::default()
                },
            )
            .unwrap();
        }
    }

    proptest! {
        #![proptest_config(NO_PERSIST)]
        #[test]
        fn test_property_name(
            propname in r#"[\w$&&[^\d]][\w$]*"#,
            whitespace_to_colon in r#"[\s&&[^\n]]*:"#,
            trailing_content in r#"\PC+"#,
        ) {
            test_regex(
                *UNQUOTED_PROPERTY_NAME_AND_COLON,
                RegexTest {
                    matches: &propname,
                    suffix: &whitespace_to_colon,
                    trailing: &trailing_content,
                    ..Default::default()
                },
            )
            .unwrap();
        }
    }

    // Test two variations of invalid unquoted property name error handling, when expecting a match
    // against the regex `UNQUOTED_PROPERTY_NAME_AND_COLON` numbered capture group pattern:
    //
    // 1) No generated test candidates match any `NEXT_TOKEN` pattern.
    // 2) The first digit is a number, which does match a `NEXT_TOKEN` capture, but is an invalid
    //    property name.
    //
    // It's challenging to write a pattern for what does NOT constitute a valid property name since
    // the set of things not part of a given set is infinite. Unicode support also can make it hard
    // to define exhaustive patterns sometimes. So here are two tests for invalid unquoted property
    // names, both of which validate that a property name cannot start with a digit. The difference
    // between the two tests is:
    //
    //   * The first test generates candidate property names that will not match any pattern in the
    //     `NEXT_TOKEN` regex, generating a "capture failed" error.
    //   * The second test successfully captures a `NEXT_TOKEN`, but it captures a number literal,
    //     not an `UNQUOTED_PROPERTY_NAME_AND_COLON`, generating a different error message:
    //     "capture group {n} did not match" (where '{n}' is the capture group number for
    //     `UNQUOTED_PROPERTY_NAME_AND_COLON`).
    ////////////////////////////////////////////////////////////////////////////////////////////////

    // Excluding 0-9, e & E, and x and X from the allowed pattern set for the second character
    // ensures the pattern generator will not generate strings with prefixes such as: `25`, `0X4`,
    // `0xf`, and `3E2`.
    proptest! {
        #![proptest_config(EXTRA_CASES_NO_PERSIST)]
        #[test]
        fn bad_property_name(
            propname in r#"[0-9][\w&&[^0-9eExX]][\w$]*"#,
            whitespace_to_colon in r#"[\s&&[^\n]]*:"#,
            trailing_content in r#"\PC+"#,
        ) {
            test_regex(
                *UNQUOTED_PROPERTY_NAME_AND_COLON,
                RegexTest {
                    error: Some("capture failed"),
                    matches: &propname,
                    suffix: &whitespace_to_colon,
                    trailing: &trailing_content,
                    ..Default::default()
                },
            )
            .unwrap();
        }
    }

    // In this case, the second character is a dollar sign, which is legal for a property name,
    // but _not_ a "Word" character in the regex `\w` pattern set. The `\b` (word boundary) applies,
    // matching the digit as the `NEXT_TOKEN`, generating an error: "capture group {n} did not
    // match" (where '{n}' is the capture group number for `UNQUOTED_PROPERTY_NAME_AND_COLON`).
    proptest! {
        #![proptest_config(EXTRA_CASES_NO_PERSIST)]
        #[test]
        fn bad_property_name_captures_number_first(
            propname in r#"[0-9]\$[\w$]*"#,
            whitespace_to_colon in r#"[\s&&[^\n]]*:"#,
            trailing_content in r#"\PC+"#,
        ) {
            test_regex(
                *UNQUOTED_PROPERTY_NAME_AND_COLON,
                RegexTest {
                    error: Some(
                        &format!("capture group {} did not match",
                        *UNQUOTED_PROPERTY_NAME_AND_COLON)
                    ),
                    matches: &propname,
                    suffix: &whitespace_to_colon,
                    trailing: &trailing_content,
                    ..Default::default()
                },
            )
            .unwrap();
        }
    }

    proptest! {
        #![proptest_config(NO_PERSIST)]
        #[test]
        fn test_single_quoted_string(
            single_quote in r#"'"#,
            single_quoted_string in r#"(([^'\\\n])|(\\')|(\\\n)|(\\\\))*"#,
            // comment inserted to balance closing braces [ and { for code editors
            non_literal_trailing_content in r#"\s*[,:/\]\}]"#,
        ) {
            test_regex(
                *OPEN_QUOTE,
                RegexTest {
                    matches: &single_quote,
                    next_regex: Some(&*SINGLE_QUOTED),
                    next_matches: &single_quoted_string,
                    next_suffix: &single_quote,
                    trailing: &non_literal_trailing_content,
                    ..Default::default()
                },
            )
            .unwrap();
        }
    }

    proptest! {
        #![proptest_config(NO_PERSIST)]
        #[test]
        fn test_double_quoted_string(
            double_quote in r#"""#,
            double_quoted_string in r#"(([^"\\\n])|(\\")|(\\\n)|(\\\\))*"#,
            // comment inserted to balance closing braces [ and { for code editors
            non_literal_trailing_content in r#"\s*[,:/\]\}]?\PC*"#,
        ) {
            test_regex(
                *OPEN_QUOTE,
                RegexTest {
                    matches: &double_quote,
                    next_regex: Some(&*DOUBLE_QUOTED),
                    next_matches: &double_quoted_string,
                    next_suffix: &double_quote,
                    trailing: &non_literal_trailing_content,
                    ..Default::default()
                },
            )
            .unwrap();
        }
    }

    proptest! {
        #![proptest_config(NO_PERSIST)]
        #[test]
        fn test_non_string_primitive(
            non_string_primitive in
                concat!(
                    r#"(null|true|false)|([-+]?(NaN|Infinity|(0[xX][0-9a-fA-F]+)"#,
                    r#"|([0-9]+[eE][+-]?[0-9]+)|([0-9]*\.[0-9]+)|([0-9]+\.?)))"#
                ),
            ends_non_string_primitive in r#"(|([\s,\]\}]\PC*))"#,
        ) {
            test_regex(
                *NON_STRING_PRIMITIVE,
                RegexTest {
                    matches: &non_string_primitive,
                    trailing: &ends_non_string_primitive,
                    ..Default::default()
                }
            )
            .unwrap();
        }
    }

    proptest! {
        #![proptest_config(NO_PERSIST)]
        #[test]
        fn test_brace(
            brace in r#"[\[\{\}\]]"#,
            // comment inserted to add a closing " since VSCode thinks prior quote is still open.
            any_chars in r#"\PC*"#,
        ) {
            test_regex(
                *BRACE,
                RegexTest { matches: &brace, trailing: &any_chars, ..Default::default() },
            )
            .unwrap();
        }
    }

    proptest! {
        #![proptest_config(NO_PERSIST)]
        #[test]
        fn test_comma(
            comma in r#","#,
            any_chars in r#"\PC*"#,
        ) {
            test_regex(
                *COMMA,
                RegexTest { matches: &comma, trailing: &any_chars, ..Default::default() },
            )
            .unwrap();
        }
    }

    proptest! {
        #![proptest_config(NO_PERSIST)]
        #[test]
        fn test_colon(
            colon in r#":"#,
            any_chars in r#"\PC*"#,
        ) {
            test_capture(
                &*COLON,
                None,
                RegexTest { matches: &colon, trailing: &any_chars, ..Default::default() },
            )
            .unwrap();
        }
    }

    #[test]
    fn test_regex_line_comment() {
        test_regex(
            *LINE_COMMENT_SLASHES,
            RegexTest {
                matches: "//",
                next_regex: Some(&*LINE_COMMENT),
                next_matches: " some line comment",
                trailing: "",
                ..Default::default()
            },
        )
        .unwrap();

        test_regex(
            *LINE_COMMENT_SLASHES,
            RegexTest {
                matches: "//",
                next_regex: Some(&*LINE_COMMENT),
                next_matches: "    some line comment",
                trailing: "\n  more lines",
                ..Default::default()
            },
        )
        .unwrap();

        test_regex(
            *LINE_COMMENT_SLASHES,
            RegexTest {
                matches: "//",
                next_regex: Some(&*LINE_COMMENT),
                trailing: "\nan empty line comment",
                ..Default::default()
            },
        )
        .unwrap();

        test_regex(
            *LINE_COMMENT_SLASHES,
            RegexTest {
                matches: "//",
                next_regex: Some(&*LINE_COMMENT),
                next_matches: "/\t    some doc comment",
                trailing: "\nmultiple lines\nare here\n",
                ..Default::default()
            },
        )
        .unwrap();
    }

    #[test]
    fn test_regex_block_comment() {
        test_regex(
            *OPEN_BLOCK_COMMENT,
            RegexTest {
                matches: "/*",
                next_regex: Some(&*BLOCK_COMMENT),
                next_matches: " this is a single line block comment ",
                next_suffix: "*/",
                trailing: "\n\nproperty: ignored",
                ..Default::default()
            },
        )
        .unwrap();

        test_regex(
            *OPEN_BLOCK_COMMENT,
            RegexTest {
                matches: "/*",
                next_regex: Some(&*BLOCK_COMMENT),
                next_matches: " this is a
            multiline block comment",
                next_suffix: "*/",
                trailing: "\n\nproperty: ignored",
                ..Default::default()
            },
        )
        .unwrap();

        test_regex(
            *OPEN_BLOCK_COMMENT,
            RegexTest {
                matches: "/*",
                next_regex: Some(&*BLOCK_COMMENT),
                next_matches: "",
                next_suffix: "*/",
                trailing: " to test an empty block comment",
                ..Default::default()
            },
        )
        .unwrap();
    }

    #[test]
    fn test_regex_non_string_primitive() {
        test_regex(*NON_STRING_PRIMITIVE, RegexTest { matches: "null", ..Default::default() })
            .unwrap();

        test_regex(
            *NON_STRING_PRIMITIVE,
            RegexTest { matches: "NULL", error: Some("capture failed"), ..Default::default() },
        )
        .unwrap();

        test_regex(
            *NON_STRING_PRIMITIVE,
            RegexTest { matches: "nullify", error: Some("capture failed"), ..Default::default() },
        )
        .unwrap();

        test_regex(*NON_STRING_PRIMITIVE, RegexTest { matches: "true", ..Default::default() })
            .unwrap();

        test_regex(
            *NON_STRING_PRIMITIVE,
            RegexTest { matches: "True", error: Some("capture failed"), ..Default::default() },
        )
        .unwrap();

        test_regex(
            *NON_STRING_PRIMITIVE,
            RegexTest { matches: "truest", error: Some("capture failed"), ..Default::default() },
        )
        .unwrap();

        test_regex(*NON_STRING_PRIMITIVE, RegexTest { matches: "false", ..Default::default() })
            .unwrap();

        for prefix in &["", "-", "+"] {
            for exp_prefix in &["", "-", "+"] {
                test_regex(
                    *NON_STRING_PRIMITIVE,
                    RegexTest {
                        matches: &(prefix.to_string() + "123e" + exp_prefix + "456"),
                        ..Default::default()
                    },
                )
                .unwrap();

                test_regex(
                    *NON_STRING_PRIMITIVE,
                    RegexTest {
                        matches: &(prefix.to_string() + "123E" + exp_prefix + "456"),
                        ..Default::default()
                    },
                )
                .unwrap();
            }

            test_regex(
                *NON_STRING_PRIMITIVE,
                RegexTest { matches: &(prefix.to_string() + "0x1a2b3e4f"), ..Default::default() },
            )
            .unwrap();

            test_regex(
                *NON_STRING_PRIMITIVE,
                RegexTest { matches: &(prefix.to_string() + "0X1a2b3e4f"), ..Default::default() },
            )
            .unwrap();

            test_regex(
                *NON_STRING_PRIMITIVE,
                RegexTest { matches: &(prefix.to_string() + "0x1A2B3E4F"), ..Default::default() },
            )
            .unwrap();

            test_regex(
                *NON_STRING_PRIMITIVE,
                RegexTest { matches: &(prefix.to_string() + "0X1a2B3e4F"), ..Default::default() },
            )
            .unwrap();

            test_regex(
                *NON_STRING_PRIMITIVE,
                RegexTest {
                    matches: &(prefix.to_string() + "0x1a2b3e4fg"),
                    error: Some("capture failed"),
                    ..Default::default()
                },
            )
            .unwrap();

            test_regex(
                *NON_STRING_PRIMITIVE,
                RegexTest {
                    matches: &(prefix.to_string() + "0X"),
                    error: Some("capture failed"),
                    ..Default::default()
                },
            )
            .unwrap();

            test_regex(*NON_STRING_PRIMITIVE, RegexTest { matches: "NaN", ..Default::default() })
                .unwrap();

            test_regex(
                *NON_STRING_PRIMITIVE,
                RegexTest { matches: "NAN", error: Some("capture failed"), ..Default::default() },
            )
            .unwrap();

            test_regex(
                *NON_STRING_PRIMITIVE,
                RegexTest { matches: "NaN0", error: Some("capture failed"), ..Default::default() },
            )
            .unwrap();

            test_regex(
                *NON_STRING_PRIMITIVE,
                RegexTest { matches: "Infinity", ..Default::default() },
            )
            .unwrap();

            test_regex(
                *NON_STRING_PRIMITIVE,
                RegexTest {
                    matches: "infinity",
                    error: Some("capture failed"),
                    ..Default::default()
                },
            )
            .unwrap();

            test_regex(
                *NON_STRING_PRIMITIVE,
                RegexTest {
                    matches: "Infinity_",
                    error: Some("capture failed"),
                    ..Default::default()
                },
            )
            .unwrap();

            test_regex(
                *NON_STRING_PRIMITIVE,
                RegexTest { matches: &(prefix.to_string() + "0"), ..Default::default() },
            )
            .unwrap();

            test_regex(
                *NON_STRING_PRIMITIVE,
                RegexTest {
                    matches: &(prefix.to_string() + "1234567890123456789012345678901234567890"),
                    ..Default::default()
                },
            )
            .unwrap();

            test_regex(
                *NON_STRING_PRIMITIVE,
                RegexTest { matches: &(prefix.to_string() + "12345.67890"), ..Default::default() },
            )
            .unwrap();

            test_regex(
                *NON_STRING_PRIMITIVE,
                RegexTest { matches: &(prefix.to_string() + ".67890"), ..Default::default() },
            )
            .unwrap();

            test_regex(
                *NON_STRING_PRIMITIVE,
                RegexTest { matches: &(prefix.to_string() + "12345."), ..Default::default() },
            )
            .unwrap();
        }
    }

    #[test]
    fn test_regex_unquoted_property_name() {
        test_regex(
            *UNQUOTED_PROPERTY_NAME_AND_COLON,
            RegexTest {
                matches: "propname",
                suffix: ":",
                trailing: " 'some property value',",
                ..Default::default()
            },
        )
        .unwrap();

        test_regex(
            *UNQUOTED_PROPERTY_NAME_AND_COLON,
            RegexTest {
                matches: "propname",
                suffix: "   :",
                trailing: " 'some property value',",
                ..Default::default()
            },
        )
        .unwrap();

        test_regex(
            *UNQUOTED_PROPERTY_NAME_AND_COLON,
            RegexTest {
                error: Some("capture failed"),
                // error: Some(&format!(
                //     "capture group {} did not match",
                //     *UNQUOTED_PROPERTY_NAME_AND_COLON
                // )),
                matches: "99propname",
                suffix: ":",
                trailing: " 'property names do not start with digits,",
                ..Default::default()
            },
        )
        .unwrap();
    }

    #[test]
    fn test_regex_string() {
        test_regex(
            *OPEN_QUOTE,
            RegexTest {
                matches: "'",
                next_regex: Some(&*SINGLE_QUOTED),
                next_matches: "this is a simple single-quoted string",
                next_suffix: "'",
                trailing: "",
                ..Default::default()
            },
        )
        .unwrap();

        test_regex(
            *OPEN_QUOTE,
            RegexTest {
                matches: "'",
                next_regex: Some(&*SINGLE_QUOTED),
                next_matches: " this is a \\
            multiline \"text\" string",
                next_suffix: "'",
                trailing: ", end of value",
                ..Default::default()
            },
        )
        .unwrap();

        test_regex(
            *OPEN_QUOTE,
            RegexTest {
                matches: "\"",
                next_regex: Some(&*DOUBLE_QUOTED),
                next_matches: "this is a simple double-quoted string",
                next_suffix: "\"",
                trailing: "",
                ..Default::default()
            },
        )
        .unwrap();

        test_regex(
            *OPEN_QUOTE,
            RegexTest {
                matches: "\"",
                next_regex: Some(&*DOUBLE_QUOTED),
                next_matches: " this is a \\
            multiline 'text' string with escaped \\\" double-quote",
                next_suffix: "\"",
                trailing: ", end of value",
                ..Default::default()
            },
        )
        .unwrap();

        test_regex(
            *OPEN_QUOTE,
            RegexTest {
                matches: "\"",
                next_regex: Some(&*DOUBLE_QUOTED),
                next_matches: "",
                next_suffix: "\"",
                trailing: ", to test empty string",
                ..Default::default()
            },
        )
        .unwrap();
    }

    #[test]
    fn test_regex_braces() {
        test_regex(*BRACE, RegexTest { matches: "[", trailing: " 1234 ]", ..Default::default() })
            .unwrap();

        test_regex(*BRACE, RegexTest { matches: "[", trailing: "true]", ..Default::default() })
            .unwrap();

        test_regex(
            *BRACE,
            RegexTest { matches: "[", trailing: "\n  'item',\n  'item2'\n]", ..Default::default() },
        )
        .unwrap();

        test_regex(*BRACE, RegexTest { matches: "]", trailing: ",[1234],", ..Default::default() })
            .unwrap();

        test_regex(*BRACE, RegexTest { matches: "{", trailing: " 1234 }", ..Default::default() })
            .unwrap();

        test_regex(*BRACE, RegexTest { matches: "{", trailing: "true}", ..Default::default() })
            .unwrap();

        test_regex(
            *BRACE,
            RegexTest { matches: "{", trailing: "\n  'item',\n  'item2'\n}", ..Default::default() },
        )
        .unwrap();

        test_regex(*BRACE, RegexTest { matches: "}", trailing: ",{1234},", ..Default::default() })
            .unwrap();
    }

    #[test]
    fn test_regex_command_colon() {
        test_regex(
            *COMMA,
            RegexTest { matches: ",", trailing: "\n  'item',\n  'item2'\n}", ..Default::default() },
        )
        .unwrap();

        test_regex(*COMMA, RegexTest { matches: ",", trailing: "{1234},", ..Default::default() })
            .unwrap();

        test_capture(&*COLON, None, RegexTest { matches: ":", ..Default::default() }).unwrap();

        test_capture(&*COLON, None, RegexTest { matches: "  \t :", ..Default::default() }).unwrap();

        test_capture(
            &*COLON,
            None,
            RegexTest { error: Some("capture failed"), matches: " \n :", ..Default::default() },
        )
        .unwrap();
    }

    #[test]
    fn test_enums() {
        let line_comment = Comment::Line("a line comment".to_owned());
        assert!(line_comment.is_line());

        let block_comment =
            Comment::Block { lines: vec!["a block".into(), "comment".into()], align: true };
        assert!(block_comment.is_block());

        let primitive_value = Primitive::new("l33t".to_owned(), vec![]);
        assert!(primitive_value.is_primitive());

        let array_value = Array::new(vec![]);
        assert!(array_value.is_array());

        let object_value = Object::new(vec![]);
        assert!(object_value.is_object());
    }

    #[test]
    fn test_document_exceeds_nesting_limit() {
        let mut parser = Parser::new(&None);
        parser.set_nesting_limit(5);
        let good_buffer = r##"{
    list_of_lists_of_lists: [[[]]]
}"##;
        parser.parse_from_location(&good_buffer, 8, 15).expect("should NOT exceed nesting limit");

        let bad_buffer = r##"{
    list_of_lists_of_lists: [[[[]]]]
}"##;
        let err = parser
            .parse_from_location(&bad_buffer, 8, 15)
            .expect_err("should exceed nesting limit");
        match err {
            Error::Parse(_, message) => {
                assert_eq!(
                    message,
                    r##"The given JSON5 document exceeds the parser's nesting limit of 5:
    list_of_lists_of_lists: [[[[]]]]
                               ^"##
                )
            }
            _ => panic!("expected a parser error"),
        }
    }

    #[test]
    fn test_parse_from_location_error_location() {
        let filename = Some("mixed_content.md".to_string());
        let mixed_document = r##"
Mixed Content Doc
=================

This is a document with embedded JSON5 content.

```json5
json5_value = {
    // The next line should generate a parser error
    999,
}
```

End of mixed content document.
"##;
        let json5_slice =
            &mixed_document[mixed_document.find("{").unwrap()..mixed_document.find("}").unwrap()];
        let mut parser = Parser::new(&filename);
        let err = parser
            .parse_from_location(json5_slice, 8, 15)
            .expect_err("check error message for location");
        match err {
            Error::Parse(Some(loc), message) => {
                assert_eq!(loc.file, Some("mixed_content.md".to_owned()));
                assert_eq!(loc.line, 10);
                assert_eq!(loc.col, 5);
                assert_eq!(
                    message,
                    r##"Object values require property names:
    999,
    ^~~"##
                )
            }
            _ => panic!("expected a parser error"),
        }
    }

    #[test]
    fn test_doc_with_nulls() {
        let mut parser = Parser::new(&None);
        let buffer = "[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[////[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]\u{000}\u{000}\u{000}\u{000}\u{000}\u{000}\u{000}\u{000}\u{000}\u{000}\u{000}\u{000}\u{000}\u{000}]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]";
        let err = parser.parse(&buffer).expect_err("should fail");
        match err {
            Error::Parse(_, message) => {
                assert!(message.starts_with("Mismatched braces in the document:"));
            }
            _ => panic!("expected a parser error"),
        }
    }
}
