// Copyright (c) 2020 Google LLC All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#![deny(missing_docs)]
use {
    crate::{error::*, formatter::*, options::*, parser::*},
    std::cell::RefCell,
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
    pub(crate) content: Array,
}

impl ParsedDocument {
    /// Parses the JSON5 document represented by `buffer`, and returns a parsed representation of
    /// the document that can be formatted by
    /// [Json5Format::to_utf8()](struct.Json5Format.html#method.to_utf8).
    ///
    /// If a filename is also provided, any parsing errors will include the filename with the line
    /// number and column where the error was encountered.
    pub fn from_str(buffer: &str, filename: Option<String>) -> Result<Self, Error> {
        let mut parser = Parser::new(buffer, &filename);
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
        let mut parser = Parser::new(&buffer, &filename);
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

#[derive(Debug)]
pub(crate) enum Comment {
    Block { lines: Vec<String>, align: bool },
    Line(String),
    Break,
}

impl Comment {
    pub fn is_block(&self) -> bool {
        match self {
            Comment::Block { .. } => true,
            _ => false,
        }
    }

    #[allow(dead_code)] // for API consistency and tests even though enum is currently not `pub`
    pub fn is_line(&self) -> bool {
        match self {
            Comment::Line(..) => true,
            _ => false,
        }
    }

    #[allow(dead_code)] // for API consistency and tests even though enum is currently not `pub`
    pub fn is_break(&self) -> bool {
        match self {
            Comment::Break => true,
            _ => false,
        }
    }

    pub fn format<'a>(&self, formatter: &'a mut Formatter) -> Result<&'a mut Formatter, Error> {
        match self {
            Comment::Block { lines, align } => {
                let len = lines.len();
                for (index, line) in lines.iter().enumerate() {
                    let is_first = index == 0;
                    let is_last = index == len - 1;
                    if is_first {
                        formatter.append(&format!("/*{}", line))?;
                    } else {
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

pub(crate) trait ValueMeta {
    fn as_value_meta(&mut self) -> &mut dyn ValueMeta;

    fn get_comments(&mut self) -> &mut Vec<Comment>;

    fn check_end_of_line_comment_is_none(&self, option: &Option<String>) -> Result<(), Error> {
        if option.is_none() {
            Ok(())
        } else {
            Err(Error::internal(
                None,
                "multiple end of line comments are currently considered \
                 ambiguous, and the parser is supposed to consider all subsequent line \
                 comments as comments for the next value.",
            ))
        }
    }

    fn set_end_of_line_comment(&mut self, comment: &str) -> Result<(), Error>;

    fn get_end_of_line_comment(&mut self) -> &Option<String>;

    fn has_comments(&mut self) -> bool {
        self.get_comments().len() > 0 || self.get_end_of_line_comment().is_some()
    }

    fn format<'a>(&self, formatter: &'a mut Formatter) -> Result<&'a mut Formatter, Error>;
}

pub(crate) enum Value {
    Array(Array),
    Object(Object),
    Primitive(Primitive),
}

impl Value {
    pub fn is_array(&self) -> bool {
        match self {
            Value::Array(..) => true,
            _ => false,
        }
    }

    pub fn is_object(&self) -> bool {
        match self {
            Value::Object(..) => true,
            _ => false,
        }
    }

    pub fn is_primitive(&self) -> bool {
        match self {
            Value::Primitive(..) => true,
            _ => false,
        }
    }

    pub fn meta(&mut self) -> &mut dyn ValueMeta {
        use Value::*;
        match self {
            Primitive(v) => v.as_value_meta(),
            Array(v) => v.as_value_meta(),
            Object(v) => v.as_value_meta(),
        }
    }
}

impl std::fmt::Debug for Value {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        use Value::*;
        match self {
            Primitive(v) => v.fmt(f),
            Array(v) => v.fmt(f),
            Object(v) => v.fmt(f),
        }
    }
}

pub(crate) struct Primitive {
    value_string: String,
    comments: Vec<Comment>,
    end_of_line_comment: Option<String>,
}

impl Primitive {
    pub fn new(value_string: String, comments: Vec<Comment>) -> Value {
        Value::Primitive(Primitive { comments, end_of_line_comment: None, value_string })
    }
}

impl ValueMeta for Primitive {
    fn as_value_meta(&mut self) -> &mut dyn ValueMeta {
        self
    }

    fn get_comments(&mut self) -> &mut Vec<Comment> {
        &mut self.comments
    }

    fn set_end_of_line_comment(&mut self, comment: &str) -> Result<(), Error> {
        self.check_end_of_line_comment_is_none(&self.end_of_line_comment)?;
        self.end_of_line_comment = Some(comment.to_string());
        Ok(())
    }

    fn get_end_of_line_comment(&mut self) -> &Option<String> {
        &self.end_of_line_comment
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

pub(crate) trait Container: ValueMeta {
    fn on_newline(&mut self) -> Result<(), Error>;

    /// Adds a standalone line comment to this container, or adds an end_of_line_comment to the
    /// current container's current value.
    ///
    /// # Arguments
    ///   * `content`: the line comment content (including leading spaces)
    ///   * `pending_new_line_comment_block` - If true and the comment is not an
    ///     end_of_line_comment, the container should insert a line_comment_break before inserting
    ///     the next line comment. This should only be true if this standalone line comment was
    ///     preceeded by one or more standalone line comments and one or more blank lines.
    ///
    /// # Returns
    ///   true if the line comment is standalone, that is, not an end_of_line_comment
    fn add_line_comment(
        &mut self,
        content: &str,
        pending_new_line_comment_block: bool,
    ) -> Result<bool, Error>;

    fn add_block_comment(&mut self, comment: Comment) -> Result<(), Error>;

    fn has_pending_comments(&self) -> bool;

    fn take_pending_comments(&mut self) -> Vec<Comment>;

    fn add_value(&mut self, value: Rc<RefCell<Value>>, parser: &Parser<'_>) -> Result<(), Error>;

    /// The parser encountered a comma, indicating the end of an element declaration. Since
    /// commas are optional, close() also indicates the end of a value without a trailing comma.
    fn end_value(&mut self, _parser: &Parser<'_>) -> Result<(), Error>;

    /// The parser encountered a closing brace indicating the end of the container's declaration.
    fn close(&mut self, _parser: &Parser<'_>) -> Result<(), Error>;

    fn format_content<'a>(&self, formatter: &'a mut Formatter) -> Result<&'a mut Formatter, Error>;
}

pub(crate) struct Array {
    /// comments applied to this Array
    comments: Vec<Comment>,

    end_of_line_comment: Option<String>,

    /// Parsed comments to be applied to the next Value, when reached.
    /// If there are any pending comments after the last item, they are written after the last
    /// item when formatting.
    pending_comments: Vec<Comment>,

    /// The array items
    items: Vec<Rc<RefCell<Value>>>,

    /// Immediately after capturing a Value (primitive or start of an object or array block),
    /// this is set to the new Value. If a line comment is captured *before* capturing a
    /// newline, the line comment is applied to the current_line_value.
    current_line_value: Option<Rc<RefCell<Value>>>,

    /// Set to true when a value is encountered (parsed primitive, or sub-container in process)
    /// and false when a comma or the array's closing brace is encountered. This supports
    /// validating that each array item is separated by one and only one comma.
    is_parsing_value: bool,
}

impl Array {
    pub fn new(comments: Vec<Comment>) -> Value {
        Value::Array(Array {
            comments,
            end_of_line_comment: None,
            pending_comments: vec![],
            items: vec![],
            current_line_value: None,
            is_parsing_value: false,
        })
    }

    /// Returns a cloned vector of item references in sorted order. The items owned by this Array
    /// retain their original order.
    fn sort_items(&self, options: &FormatOptions) -> Vec<Rc<RefCell<Value>>> {
        let mut items = self.items.clone();
        if options.sort_array_items {
            items.sort_by(|left, right| {
                let left: &Value = &*left.borrow();
                let right: &Value = &*right.borrow();
                if let Value::Primitive(left_primitive) = left {
                    if let Value::Primitive(right_primitive) = right {
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
}

impl ValueMeta for Array {
    fn as_value_meta(&mut self) -> &mut dyn ValueMeta {
        self
    }

    fn get_comments(&mut self) -> &mut Vec<Comment> {
        &mut self.comments
    }

    fn set_end_of_line_comment(&mut self, comment: &str) -> Result<(), Error> {
        self.check_end_of_line_comment_is_none(&self.end_of_line_comment)?;
        self.end_of_line_comment = Some(comment.to_string());
        Ok(())
    }

    fn get_end_of_line_comment(&mut self) -> &Option<String> {
        &self.end_of_line_comment
    }

    fn format<'a>(&self, formatter: &'a mut Formatter) -> Result<&'a mut Formatter, Error> {
        formatter.format_container("[", "]", |formatter| self.format_content(formatter))
    }
}

impl Container for Array {
    fn on_newline(&mut self) -> Result<(), Error> {
        self.current_line_value = None;
        Ok(())
    }

    fn add_line_comment(
        &mut self,
        content: &str,
        pending_new_line_comment_block: bool,
    ) -> Result<bool, Error> {
        if let Some(value_ref) = &mut self.current_line_value {
            (*value_ref.borrow_mut()).meta().set_end_of_line_comment(content)?;
            Ok(false)
        } else {
            if pending_new_line_comment_block {
                self.pending_comments.push(Comment::Break);
            }
            self.pending_comments.push(Comment::Line(content.to_string()));
            Ok(true)
        }
    }

    fn add_block_comment(&mut self, comment: Comment) -> Result<(), Error> {
        self.pending_comments.push(comment);
        Ok(())
    }

    fn has_pending_comments(&self) -> bool {
        self.pending_comments.len() > 0
    }

    fn take_pending_comments(&mut self) -> Vec<Comment> {
        self.pending_comments.drain(..).collect()
    }

    fn add_value(&mut self, value: Rc<RefCell<Value>>, parser: &Parser<'_>) -> Result<(), Error> {
        if self.is_parsing_value {
            Err(parser.error("Array items must be separated by a comma"))
        } else {
            self.is_parsing_value = true;
            self.current_line_value = Some(value.clone());
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
        self.is_parsing_value = false;
        Ok(())
    }

    fn format_content<'a>(&self, formatter: &'a mut Formatter) -> Result<&'a mut Formatter, Error> {
        let sorted_items = self.sort_items(&formatter.options_in_scope());
        let len = sorted_items.len();
        for (index, item) in sorted_items.iter().enumerate() {
            let is_first = index == 0;
            let is_last = index == len - 1;
            formatter.format_item(item, is_first, is_last, self.has_pending_comments())?;
        }

        formatter.format_trailing_comments(&self.pending_comments)
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

#[derive(Clone)]
pub(crate) struct Property {
    /// An unquoted or quoted property name. If unquoted, the name must match the
    /// UNQUOTED_PROPERTY_NAME_PATTERN.
    pub name: String,

    /// The property value.
    pub value: Rc<RefCell<Value>>,
}

impl Property {
    pub fn new(name: String, value: Rc<RefCell<Value>>) -> Self {
        Property { name, value }
    }
}

impl std::fmt::Debug for Property {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        write!(f, "Property {}: {:?}", self.name, self.value)
    }
}

pub(crate) struct Object {
    /// comments applied to this Object
    comments: Vec<Comment>,

    end_of_line_comment: Option<String>,

    /// Parsed comments to be applied to the next Value, when reached.
    /// If there are any pending comments after the last property, they are written after the last
    /// property when formatting.
    pending_comments: Vec<Comment>,

    /// Parsed property name to be applied to the next upcoming Value.
    pending_property_name: Option<String>,

    /// Properties of this object.
    properties: Vec<Property>,

    /// Immediately after capturing a Value (primitive or start of an object or array block),
    /// this is set to the new Value. If a line comment is captured *before* capturing a
    /// newline, the line comment is applied to the current_line_value.
    current_line_value: Option<Rc<RefCell<Value>>>,

    /// Set to true when a value is encountered (parsed primitive, or sub-container in process)
    /// and false when a comma or the object's closing brace is encountered. This supports
    /// validating that each property is separated by one and only one comma.
    is_parsing_property: bool,
}

impl Object {
    pub fn new(comments: Vec<Comment>) -> Value {
        Value::Object(Object {
            comments,
            pending_comments: vec![],
            end_of_line_comment: None,
            pending_property_name: None,
            properties: vec![],
            current_line_value: None,
            is_parsing_property: false,
        })
    }

    /// The given property name was parsed. Once it's value is also parsed, the property will be
    /// added to this `Object`.
    ///
    /// # Arguments
    ///   * name - the property name, possibly quoted
    ///   * parser - reference to the current state of the parser
    pub fn set_pending_property(&mut self, name: String, parser: &Parser<'_>) -> Result<(), Error> {
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
    pub fn has_pending_property(&mut self) -> Result<bool, Error> {
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
}

impl ValueMeta for Object {
    fn as_value_meta(&mut self) -> &mut dyn ValueMeta {
        self
    }

    fn get_comments(&mut self) -> &mut Vec<Comment> {
        &mut self.comments
    }

    fn set_end_of_line_comment(&mut self, comment: &str) -> Result<(), Error> {
        self.check_end_of_line_comment_is_none(&self.end_of_line_comment)?;
        self.end_of_line_comment = Some(comment.to_string());
        Ok(())
    }

    fn get_end_of_line_comment(&mut self) -> &Option<String> {
        &self.end_of_line_comment
    }

    fn format<'a>(&self, formatter: &'a mut Formatter) -> Result<&'a mut Formatter, Error> {
        formatter.format_container("{", "}", |formatter| self.format_content(formatter))
    }
}

impl Container for Object {
    fn on_newline(&mut self) -> Result<(), Error> {
        self.current_line_value = None;
        Ok(())
    }

    fn add_line_comment(
        &mut self,
        content: &str,
        pending_new_line_comment_block: bool,
    ) -> Result<bool, Error> {
        if let Some(value_ref) = &mut self.current_line_value {
            (*value_ref.borrow_mut()).meta().set_end_of_line_comment(content)?;
            Ok(false)
        } else {
            if pending_new_line_comment_block {
                self.pending_comments.push(Comment::Break);
            }
            self.pending_comments.push(Comment::Line(content.to_string()));
            Ok(true)
        }
    }

    fn add_block_comment(&mut self, comment: Comment) -> Result<(), Error> {
        self.pending_comments.push(comment);
        Ok(())
    }

    fn has_pending_comments(&self) -> bool {
        self.pending_comments.len() > 0
    }

    fn take_pending_comments(&mut self) -> Vec<Comment> {
        self.pending_comments.drain(..).collect()
    }

    fn add_value(&mut self, value: Rc<RefCell<Value>>, parser: &Parser<'_>) -> Result<(), Error> {
        match self.pending_property_name.take() {
            Some(name) => {
                self.current_line_value = Some(value.clone());
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
            None => {
                self.is_parsing_property = false;
                Ok(())
            }
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
            formatter.format_property(&property, is_first, is_last, self.has_pending_comments())?;
        }

        formatter.format_trailing_comments(&self.pending_comments)
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
