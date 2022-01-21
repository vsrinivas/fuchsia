// Copyright (c) 2020 Google LLC All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#![deny(missing_docs)]
use {
    crate::{error::*, formatter::*, options::*, parser::*},
    std::cell::{Ref, RefCell, RefMut},
    std::cmp::Ordering,
    std::rc::Rc,
};

/// Represents the parsed state of a given JSON5 document.
pub struct ParsedDocument {
    /// The saved document input buffer, if constructed with from_string().
    owned_buffer: Option<String>,

    /// The input filename, if any.
    filename: Option<String>,

    /// The parsed document model represented as an array of zero or more objects to format.
    pub content: Array,
}

impl ParsedDocument {
    /// Parses the JSON5 document represented by `buffer`, and returns a parsed representation of
    /// the document that can be formatted by
    /// [Json5Format::to_utf8()](struct.Json5Format.html#method.to_utf8).
    ///
    /// If a filename is also provided, any parsing errors will include the filename with the line
    /// number and column where the error was encountered.
    pub fn from_str(buffer: &str, filename: Option<String>) -> Result<Self, Error> {
        Self::from_str_with_nesting_limit(buffer, filename, Parser::DEFAULT_NESTING_LIMIT)
    }

    /// Like `from_str()` but also overrides the default nesting limit, used to
    /// catch deeply nested JSON5 documents before overflowing the program
    /// stack.
    pub fn from_str_with_nesting_limit(
        buffer: &str,
        filename: Option<String>,
        nesting_limit: usize,
    ) -> Result<Self, Error> {
        let mut parser = Parser::new(&filename);
        parser.set_nesting_limit(nesting_limit);
        let content = parser.parse(&buffer)?;

        Ok(Self { owned_buffer: None, filename, content })
    }

    /// Parses the JSON5 document represented by `buffer`, and returns a parsed representation of
    /// the document that can be formatted by
    /// [Json5Format::to_utf8()](struct.Json5Format.html#method.to_utf8).
    ///
    /// The returned `ParsedDocument` object retains ownership of the input buffer, which can be
    /// useful in situations where borrowing the buffer (via
    /// [from_str()](struct.ParsedDocument.html#method.from_str) requires burdensome workarounds.
    ///
    /// If a filename is also provided, any parsing errors will include the filename with the line
    /// number and column where the error was encountered.
    pub fn from_string(buffer: String, filename: Option<String>) -> Result<Self, Error> {
        let mut parser = Parser::new(&filename);
        let content = parser.parse(&buffer)?;

        Ok(Self { owned_buffer: Some(buffer), filename, content })
    }

    /// Returns the filename, if provided when the object was created.
    pub fn filename(&self) -> &Option<String> {
        &self.filename
    }

    /// Borrows the input buffer owned by this object, if provided by calling
    /// [from_string()](struct.ParsedDocument.html#method.from_string).
    pub fn input_buffer(&self) -> &Option<String> {
        &self.owned_buffer
    }
}

/// Represents the variations of allowable comments.
#[derive(Debug, Clone)]
pub enum Comment {
    /// Represents a comment read from a `/* */` pattern.
    Block {
        /// The content of the block comment, represented as a `String` for each line.
        lines: Vec<String>,
        /// `align` (if true) indicates that all comment `lines` started in a column after the
        /// star's column in the opening `/*`. For each subsequent line in lines, the spaces from
        /// column 0 to the star's column will be stripped, allowing the indent spaces to be
        /// restored, during format, relative to the block's new horizontal position. Otherwise, the
        /// original indentation will not be stripped, and the lines will be restored at their
        /// original horizontal position. In either case, lines after the opening `/*` will retain
        /// their original horizontal alignment, relative to one another.
        align: bool,
    },

    /// Represents a comment read from a line starting with `//`.
    Line(String),

    /// Represents a blank line between data.
    Break,
}

impl Comment {
    /// Returns `true` if the `Comment` instance is a `Block` variant.
    pub fn is_block(&self) -> bool {
        match self {
            Comment::Block { .. } => true,
            _ => false,
        }
    }

    /// Returns `true` if the `Comment` instance is a `Line` variant.
    #[allow(dead_code)] // for API consistency and tests even though enum is currently not `pub`
    pub fn is_line(&self) -> bool {
        match self {
            Comment::Line(..) => true,
            _ => false,
        }
    }

    /// Returns `true` if the `Comment` instance is a `Break` variant.
    #[allow(dead_code)] // for API consistency and tests even though enum is currently not `pub`
    pub fn is_break(&self) -> bool {
        match self {
            Comment::Break => true,
            _ => false,
        }
    }

    pub(crate) fn format<'a>(
        &self,
        formatter: &'a mut Formatter,
    ) -> Result<&'a mut Formatter, Error> {
        match self {
            Comment::Block { lines, align } => {
                let len = lines.len();
                for (index, line) in lines.iter().enumerate() {
                    let is_first = index == 0;
                    let is_last = index == len - 1;
                    if is_first {
                        formatter.append(&format!("/*{}", line))?;
                    } else if line.len() > 0 {
                        formatter.append(&format!("{}", line))?;
                    }
                    if !is_last {
                        if *align {
                            formatter.start_next_line()?;
                        } else {
                            formatter.append_newline()?;
                        }
                    }
                }
                formatter.append("*/")
            }
            Comment::Line(comment) => formatter.append(&format!("//{}", comment)),
            Comment::Break => Ok(&mut *formatter), // inserts blank line only
        }?;
        formatter.start_next_line()
    }
}

/// A struct containing all comments associated with a specific `Value`.
#[derive(Clone)]
pub struct Comments {
    /// Comments applied to the associated value.
    before_value: Vec<Comment>,

    /// A line comment positioned after and on the same line as the last character of the value. The
    /// comment may have multiple lines, if parsed as a contiguous group of line comments that are
    /// all left-aligned with the initial line comment.
    end_of_line_comment: Option<String>,
}

impl Comments {
    /// Retrieves the comments immediately before an associated value.
    pub fn before_value(&self) -> &Vec<Comment> {
        &self.before_value
    }

    /// Injects text into the end-of-line comment.
    pub fn append_end_of_line_comment(&mut self, comment: &str) -> Result<(), Error> {
        let updated = match self.end_of_line_comment.take() {
            None => comment.to_string(),
            Some(current) => current + "\n" + comment,
        };
        self.end_of_line_comment = Some(updated);
        Ok(())
    }

    /// Retrieves a reference to the end-of-line comment.
    pub fn end_of_line(&self) -> &Option<String> {
        &self.end_of_line_comment
    }
}

/// A struct used for capturing comments at the end of an JSON5 array or object, which are not
/// associated to any of the Values contained in the array/object.
pub(crate) struct ContainedComments {
    /// Parsed comments to be applied to the next Value, when reached.
    /// If there are any pending comments after the last item, they are written after the last
    /// item when formatting.
    pending_comments: Vec<Comment>,

    /// Immediately after capturing a Value (primitive or start of an object or array block),
    /// this is set to the new Value. If a line comment is captured *before* capturing a
    /// newline, the line comment is applied to the current_line_value.
    current_line_value: Option<Rc<RefCell<Value>>>,

    /// If an end-of-line comment is captured after capturing a Value, this saves the column of the
    /// first character in the line comment. Successive line comments with the same start column
    /// are considered continuations of the end-of-line comment.
    end_of_line_comment_start_column: Option<usize>,
}

impl ContainedComments {
    fn new() -> Self {
        Self {
            pending_comments: vec![],
            current_line_value: None,
            end_of_line_comment_start_column: None,
        }
    }

    /// After parsing a value, if a newline is encountered before an end-of-line comment, the
    /// current line no longer has the value.
    fn on_newline(&mut self) -> Result<(), Error> {
        if self.end_of_line_comment_start_column.is_none() {
            self.current_line_value = None;
        }
        Ok(())
    }

    /// Adds a standalone line comment to this container, or adds an end_of_line_comment to the
    /// current container's current value.
    ///
    /// # Arguments
    ///   * `content`: the line comment content (including leading spaces)
    ///   * `start_column`: the column number of the first character of content. If this line
    ///     comment was immediately preceded by an end-of-line comment, and both line comments
    ///     have the same start_column, then this line comment is a continuation of the end-of-line
    ///     comment (on a new line). Formatting should retain the associated vertical alignment.
    ///   * `pending_new_line_comment_block` - If true and the comment is not an
    ///     end_of_line_comment, the container should insert a line_comment_break before inserting
    ///     the next line comment. This should only be true if this standalone line comment was
    ///     preceded by one or more standalone line comments and one or more blank lines.
    ///     (This flag is ignored if the comment is part of an end-of-line comment.)
    ///
    /// # Returns
    ///   true if the line comment is standalone, that is, not an end_of_line_comment
    fn add_line_comment(
        &mut self,
        content: &str,
        start_column: usize,
        pending_new_line_comment_block: bool,
    ) -> Result<bool, Error> {
        if let Some(value_ref) = &mut self.current_line_value {
            if start_column == *self.end_of_line_comment_start_column.get_or_insert(start_column) {
                (*value_ref.borrow_mut()).comments_mut().append_end_of_line_comment(content)?;
                return Ok(false); // the comment is (part of) an end-of-line comment
            }
            self.current_line_value = None;
        }
        if pending_new_line_comment_block {
            self.pending_comments.push(Comment::Break);
        }
        self.pending_comments.push(Comment::Line(content.to_string()));
        Ok(true)
    }

    /// Add a block comment, to be applied to the next contained value, or to the end of the current
    /// container.
    fn add_block_comment(&mut self, comment: Comment) -> Result<(), Error> {
        self.current_line_value = None;
        self.pending_comments.push(comment);
        Ok(())
    }

    /// There are one or more line and/or block comments to be applied to the next contained value,
    /// or to the end of the current container.
    fn has_pending_comments(&self) -> bool {
        self.pending_comments.len() > 0
    }

    /// When a value is encountered inside the current container, move all pending comments from the
    /// container to the new value.
    fn take_pending_comments(&mut self) -> Vec<Comment> {
        self.pending_comments.drain(..).collect()
    }
}

/// Represents the possible data types in a JSON5 object. Each variant has a field representing a
/// specialized struct representing the value's data, and a field for comments (possibly including a
/// line comment and comments appearing immediately before the value). For `Object` and `Array`,
/// comments appearing at the end of the the structure are encapsulated inside the appropriate
/// specialized struct.
pub enum Value {
    /// Represents a non-recursive data type (string, bool, number, or "null") and its associated
    /// comments.
    Primitive {
        /// The struct containing the associated value.
        val: Primitive,
        /// The associated comments.
        comments: Comments,
    },
    /// Represents a JSON5 array and its associated comments.
    Array {
        /// The struct containing the associated value.
        val: Array,
        /// The comments associated with the array.
        comments: Comments,
    },
    /// Represents a JSON5 object and its associated comments.
    Object {
        /// The struct containing the associated value.
        val: Object,
        /// The comments associated with the object.
        comments: Comments,
    },
}

impl Value {
    /// Returns `true` for an `Array` variant.
    pub fn is_array(&self) -> bool {
        match self {
            Value::Array { .. } => true,
            _ => false,
        }
    }

    /// Returns `true` for an `Object` variant.
    pub fn is_object(&self) -> bool {
        match self {
            Value::Object { .. } => true,
            _ => false,
        }
    }

    /// Returns `true` for a `Primitive` variant.
    pub fn is_primitive(&self) -> bool {
        match self {
            Value::Primitive { .. } => true,
            _ => false,
        }
    }

    /// Recursively formats the data inside a `Value`.
    pub(crate) fn format<'a>(
        &self,
        formatter: &'a mut Formatter,
    ) -> Result<&'a mut Formatter, Error> {
        use Value::*;
        match self {
            Primitive { val, .. } => val.format(formatter),
            Array { val, .. } => val.format(formatter),
            Object { val, .. } => val.format(formatter),
        }
    }

    /// Retrieves an immutable reference to the `comments` attribute of any variant.
    pub fn comments(&self) -> &Comments {
        use Value::*;
        match self {
            Primitive { comments, .. } | Array { comments, .. } | Object { comments, .. } => {
                comments
            }
        }
    }
    /// Returns a mutable reference to the `comments` attribute of any variant.
    pub fn comments_mut(&mut self) -> &mut Comments {
        use Value::*;
        match self {
            Primitive { comments, .. } | Array { comments, .. } | Object { comments, .. } => {
                comments
            }
        }
    }

    /// Returns true if this value has any block, line, or end-of-line comment(s).
    pub fn has_comments(&mut self) -> bool {
        let comments = self.comments();
        comments.before_value().len() > 0 || comments.end_of_line().is_some()
    }
}

impl std::fmt::Debug for Value {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        use Value::*;
        match self {
            Primitive { val, .. } => val.fmt(f),
            Array { val, .. } => val.fmt(f),
            Object { val, .. } => val.fmt(f),
        }
    }
}

/// Represents a primitive value in a JSON5 object property or array item.
/// The parsed value is stored as a formatted string, retaining its original format,
/// and written to the formatted document just as it appeared.
pub struct Primitive {
    value_string: String,
}

impl Primitive {
    /// Instantiates a `Value::Array` with empty data and the provided comments.
    pub(crate) fn new(value_string: String, comments: Vec<Comment>) -> Value {
        Value::Primitive {
            val: Primitive { value_string },
            comments: Comments { before_value: comments, end_of_line_comment: None },
        }
    }

    /// Returns the primitive value, as a formatted string.
    #[inline]
    pub fn as_str(&self) -> &str {
        &self.value_string
    }

    fn format<'a>(&self, formatter: &'a mut Formatter) -> Result<&'a mut Formatter, Error> {
        formatter.append(&self.value_string)
    }
}

impl std::fmt::Debug for Primitive {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        write!(f, "Primitive: {}", self.value_string)
    }
}

/// An interface that represents the recursive nature of `Object` and `Array`.
pub(crate) trait Container {
    /// Called by the `Parser` to add a parsed `Value` to the current `Container`.
    fn add_value(&mut self, value: Rc<RefCell<Value>>, parser: &Parser<'_>) -> Result<(), Error>;

    /// The parser encountered a comma, indicating the end of an element declaration. Since
    /// commas are optional, close() also indicates the end of a value without a trailing comma.
    fn end_value(&mut self, _parser: &Parser<'_>) -> Result<(), Error>;

    /// The parser encountered a closing brace indicating the end of the container's declaration.
    fn close(&mut self, _parser: &Parser<'_>) -> Result<(), Error>;

    /// Formats the content of a container (inside the braces) in accordance with the JSON5 syntax
    /// and given format options.
    fn format_content<'a>(&self, formatter: &'a mut Formatter) -> Result<&'a mut Formatter, Error>;

    /// Retrieves an immutable reference to the `contained_comments` attribute.
    fn contained_comments(&self) -> &ContainedComments;

    /// Retrieves a mutable reference to the `contained_comments` attribute.
    fn contained_comments_mut(&mut self) -> &mut ContainedComments;

    /// See `ContainedComments::on_newline`.
    fn on_newline(&mut self) -> Result<(), Error> {
        self.contained_comments_mut().on_newline()
    }

    /// See `ContainedComments::add_line_comment`.
    fn add_line_comment(
        &mut self,
        content: &str,
        start_column: usize,
        pending_new_line_comment_block: bool,
    ) -> Result<bool, Error> {
        self.contained_comments_mut().add_line_comment(
            content,
            start_column,
            pending_new_line_comment_block,
        )
    }

    /// See `ContainedComments::add_block_comment`.
    fn add_block_comment(&mut self, comment: Comment) -> Result<(), Error> {
        self.contained_comments_mut().add_block_comment(comment)
    }

    /// See `ContainedComments::has_pending_comments`.
    fn has_pending_comments(&self) -> bool {
        self.contained_comments().has_pending_comments()
    }

    /// See `ContainedComments::take_pending_comments`.
    fn take_pending_comments(&mut self) -> Vec<Comment> {
        self.contained_comments_mut().take_pending_comments()
    }
}

/// Represents a JSON5 array of items. During parsing, this object's state changes, as comments and
/// items are encountered. Parsed comments are temporarily stored in contained_comments, to be
/// transferred to the next parsed item. After the last item, if any other comments are encountered,
/// those comments are retained in the contained_comments field, to be restored during formatting,
/// after writing the last item.
pub struct Array {
    /// The array items.
    items: Vec<Rc<RefCell<Value>>>,

    /// Set to true when a value is encountered (parsed primitive, or sub-container in process)
    /// and false when a comma or the array's closing brace is encountered. This supports
    /// validating that each array item is separated by one and only one comma.
    is_parsing_value: bool,

    /// Manages parsed comments inside the array scope, which are either transferred to each array
    /// item, or retained for placement after the last array item.
    contained_comments: ContainedComments,
}

impl Array {
    /// Instantiates a `Value::Array` with empty data and the provided comments.
    pub(crate) fn new(comments: Vec<Comment>) -> Value {
        Value::Array {
            val: Array {
                items: vec![],
                is_parsing_value: false,
                contained_comments: ContainedComments::new(),
            },
            comments: Comments { before_value: comments, end_of_line_comment: None },
        }
    }

    /// Returns an iterator over the array items. Items must be dereferenced to access
    /// the `Value`. For example:
    ///
    /// ```
    /// use json5format::*;
    /// let parsed_document = ParsedDocument::from_str("{}", None)?;
    /// for item in parsed_document.content.items() {
    ///     assert!(!(*item).is_primitive());
    /// }
    /// # Ok::<(),anyhow::Error>(())
    /// ```
    pub fn items(&self) -> impl Iterator<Item = Ref<'_, Value>> {
        self.items.iter().map(|rc| rc.borrow())
    }

    /// As in `Array::items`, returns an iterator over the array items, but with mutable references.
    #[inline]
    pub fn items_mut(&mut self) -> impl Iterator<Item = RefMut<'_, Value>> {
        self.items.iter_mut().map(|rc| rc.borrow_mut())
    }

    /// Returns a reference to the comments at the end of an array not associated with any values.
    #[inline]
    pub fn trailing_comments(&self) -> &Vec<Comment> {
        &self.contained_comments.pending_comments
    }

    /// Returns a mutable reference to the comments at the end of an array not associated with any
    /// values.
    #[inline]
    pub fn trailing_comments_mut(&mut self) -> &mut Vec<Comment> {
        &mut self.contained_comments.pending_comments
    }

    /// Returns a cloned vector of item references in sorted order. The items owned by this Array
    /// retain their original order.
    fn sort_items(&self, options: &FormatOptions) -> Vec<Rc<RefCell<Value>>> {
        let mut items = self.items.clone();
        if options.sort_array_items {
            items.sort_by(|left, right| {
                let left: &Value = &*left.borrow();
                let right: &Value = &*right.borrow();
                if let Value::Primitive { val: left_primitive, .. } = left {
                    if let Value::Primitive { val: right_primitive, .. } = right {
                        let mut ordering = left_primitive
                            .value_string
                            .to_lowercase()
                            .cmp(&right_primitive.value_string.to_lowercase());
                        // If two values are case-insensitively equal, compare them again with
                        // case-sensitivity to ensure consistent re-ordering.
                        if ordering == Ordering::Equal {
                            ordering =
                                left_primitive.value_string.cmp(&right_primitive.value_string);
                        }
                        ordering
                    } else {
                        Ordering::Equal
                    }
                } else {
                    Ordering::Equal
                }
            });
        }
        items
    }

    fn format<'a>(&self, formatter: &'a mut Formatter) -> Result<&'a mut Formatter, Error> {
        formatter.format_container("[", "]", |formatter| self.format_content(formatter))
    }
}

impl Container for Array {
    fn add_value(&mut self, value: Rc<RefCell<Value>>, parser: &Parser<'_>) -> Result<(), Error> {
        if self.is_parsing_value {
            Err(parser.error("Array items must be separated by a comma"))
        } else {
            self.is_parsing_value = true;
            self.contained_comments.current_line_value = Some(value.clone());
            self.contained_comments.end_of_line_comment_start_column = None;
            self.items.push(value);
            Ok(())
        }
    }

    fn end_value(&mut self, parser: &Parser<'_>) -> Result<(), Error> {
        if self.is_parsing_value {
            self.is_parsing_value = false;
            Ok(())
        } else {
            Err(parser.error("Unexpected comma without a preceding array item value"))
        }
    }

    fn close(&mut self, _parser: &Parser<'_>) -> Result<(), Error> {
        Ok(())
    }

    fn format_content<'a>(&self, formatter: &'a mut Formatter) -> Result<&'a mut Formatter, Error> {
        let sorted_items = self.sort_items(&formatter.options_in_scope());
        let len = sorted_items.len();
        for (index, item) in sorted_items.iter().enumerate() {
            let is_first = index == 0;
            let is_last = index == len - 1;
            formatter.format_item(
                item,
                is_first,
                is_last,
                self.contained_comments.has_pending_comments(),
            )?;
        }

        formatter.format_trailing_comments(&self.contained_comments.pending_comments)
    }

    fn contained_comments(&self) -> &ContainedComments {
        &self.contained_comments
    }

    fn contained_comments_mut(&mut self) -> &mut ContainedComments {
        &mut self.contained_comments
    }
}

impl std::fmt::Debug for Array {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        write!(
            f,
            "Array of {} item{}",
            self.items.len(),
            if self.items.len() == 1 { "" } else { "s" }
        )
    }
}

/// Represents a name-value pair for a field in a JSON5 object.
#[derive(Clone)]
pub struct Property {
    /// An unquoted or quoted property name. If unquoted, the name must match the
    /// UNQUOTED_PROPERTY_NAME_PATTERN.
    pub(crate) name: String,

    /// The property value.
    pub(crate) value: Rc<RefCell<Value>>,
}

impl Property {
    /// Returns a new instance of a `Property` with the name provided as a `String`
    /// and value provided as indirection to a `Value`.
    pub(crate) fn new(name: String, value: Rc<RefCell<Value>>) -> Self {
        Property { name, value }
    }

    /// An unquoted or quoted property name. If unquoted, JSON5 property
    /// names comply with the ECMAScript 5.1 `IdentifierName` requirements.
    #[inline]
    pub fn name(&self) -> &str {
        return &self.name;
    }

    /// Returns a `Ref` to the property's value, which can be accessed by dereference,
    /// for example: `(*some_prop.value()).is_primitive()`.
    #[inline]
    pub fn value(&self) -> Ref<'_, Value> {
        return self.value.borrow();
    }

    /// Returns a `RefMut` to the property's value, which can be accessed by dereference,
    /// for example: `(*some_prop.value()).is_primitive()`.
    #[inline]
    pub fn value_mut(&mut self) -> RefMut<'_, Value> {
        return self.value.borrow_mut();
    }
}

impl std::fmt::Debug for Property {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        write!(f, "Property {}: {:?}", self.name, self.value)
    }
}

/// A specialized struct to represent the data of JSON5 object, including any comments placed at
/// the end of the object.
pub struct Object {
    /// Parsed property name to be applied to the next upcoming Value.
    pending_property_name: Option<String>,

    /// Properties of this object.
    properties: Vec<Property>,

    /// Set to true when a value is encountered (parsed primitive, or sub-container in process)
    /// and false when a comma or the object's closing brace is encountered. This supports
    /// validating that each property is separated by one and only one comma.
    is_parsing_property: bool,

    /// Manages parsed comments inside the object scope, which are either transferred to each object
    /// item, or retained for placement after the last object item.
    contained_comments: ContainedComments,
}

impl Object {
    /// Instantiates a `Value::Object` with empty data and the provided comments.
    pub(crate) fn new(comments: Vec<Comment>) -> Value {
        Value::Object {
            val: Object {
                pending_property_name: None,
                properties: vec![],
                is_parsing_property: false,
                contained_comments: ContainedComments::new(),
            },
            comments: Comments { before_value: comments, end_of_line_comment: None },
        }
    }

    /// Retrieves an iterator from the `properties` field.
    #[inline]
    pub fn properties(&self) -> impl Iterator<Item = &Property> {
        self.properties.iter()
    }

    /// Retrieves an iterator of mutable references from the `properties` field.
    #[inline]
    pub fn properties_mut(&mut self) -> impl Iterator<Item = &mut Property> {
        self.properties.iter_mut()
    }

    /// Returns a reference to the comments at the end of an object not associated with any values.
    #[inline]
    pub fn trailing_comments(&self) -> &Vec<Comment> {
        &self.contained_comments.pending_comments
    }

    /// Returns a mutable reference to the comments at the end of an object not associated with any
    /// values.
    #[inline]
    pub fn trailing_comments_mut(&mut self) -> &mut Vec<Comment> {
        &mut self.contained_comments.pending_comments
    }
    /// The given property name was parsed. Once it's value is also parsed, the property will be
    /// added to this `Object`.
    ///
    /// # Arguments
    ///   * name - the property name, possibly quoted
    ///   * parser - reference to the current state of the parser
    pub(crate) fn set_pending_property(
        &mut self,
        name: String,
        parser: &Parser<'_>,
    ) -> Result<(), Error> {
        self.contained_comments.current_line_value = None;
        if self.is_parsing_property {
            Err(parser.error("Properties must be separated by a comma"))
        } else {
            self.is_parsing_property = true;
            match &self.pending_property_name {
                Some(property_name) => Err(Error::internal(
                    parser.location(),
                    format!(
                        "Unexpected property '{}' encountered before completing the previous \
                         property '{}'",
                        name, property_name
                    ),
                )),
                None => {
                    self.pending_property_name = Some(name.to_string());
                    Ok(())
                }
            }
        }
    }

    /// Returns true if a property name has been parsed, and the parser has not yet reached a value.
    pub(crate) fn has_pending_property(&mut self) -> Result<bool, Error> {
        Ok(self.pending_property_name.is_some())
    }

    /// Returns a cloned vector of property references in sorted order. The properties owned by
    /// this Object retain their original order.
    fn sort_properties(&self, options: &SubpathOptions) -> Vec<Property> {
        let mut properties = self.properties.clone();
        properties.sort_by(|left, right| {
            options
                .get_property_priority(&left.name)
                .cmp(&options.get_property_priority(&right.name))
        });
        properties
    }

    fn format<'a>(&self, formatter: &'a mut Formatter) -> Result<&'a mut Formatter, Error> {
        formatter.format_container("{", "}", |formatter| self.format_content(formatter))
    }
}

impl Container for Object {
    fn add_value(&mut self, value: Rc<RefCell<Value>>, parser: &Parser<'_>) -> Result<(), Error> {
        match self.pending_property_name.take() {
            Some(name) => {
                self.contained_comments.current_line_value = Some(value.clone());
                self.contained_comments.end_of_line_comment_start_column = None;
                self.properties.push(Property::new(name, value));
                Ok(())
            }
            None => Err(parser.error("Object values require property names")),
        }
    }

    fn end_value(&mut self, parser: &Parser<'_>) -> Result<(), Error> {
        match &self.pending_property_name {
            Some(property_name) => Err(parser.error(format!(
                "Property '{}' must have a value before the next comma-separated property",
                property_name
            ))),
            None => {
                if self.is_parsing_property {
                    self.is_parsing_property = false;
                    Ok(())
                } else {
                    Err(parser.error("Unexpected comma without a preceding property"))
                }
            }
        }
    }

    fn close(&mut self, parser: &Parser<'_>) -> Result<(), Error> {
        match &self.pending_property_name {
            Some(property_name) => Err(parser.error(format!(
                "Property '{}' must have a value before closing an object",
                property_name
            ))),
            None => Ok(()),
        }
    }

    fn format_content<'a>(&self, formatter: &'a mut Formatter) -> Result<&'a mut Formatter, Error> {
        let sorted_properties = match formatter.get_current_subpath_options() {
            Some(options) => Some(self.sort_properties(&*options.borrow())),
            None => None,
        };
        let properties = match &sorted_properties {
            Some(sorted_properties) => &sorted_properties,
            None => &self.properties,
        };

        let len = properties.len();
        for (index, property) in properties.iter().enumerate() {
            let is_first = index == 0;
            let is_last = index == len - 1;
            formatter.format_property(
                &property,
                is_first,
                is_last,
                self.contained_comments.has_pending_comments(),
            )?;
        }

        formatter.format_trailing_comments(&self.contained_comments.pending_comments)
    }

    fn contained_comments(&self) -> &ContainedComments {
        &self.contained_comments
    }

    fn contained_comments_mut(&mut self) -> &mut ContainedComments {
        &mut self.contained_comments
    }
}

impl std::fmt::Debug for Object {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        write!(
            f,
            "Object of {} propert{}",
            self.properties.len(),
            if self.properties.len() == 1 { "y" } else { "ies" }
        )
    }
}
