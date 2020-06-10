// Copyright (c) 2020 Google LLC All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#![deny(missing_docs)]
use {
    crate::{content::*, error::*, options::*},
    std::cell::RefCell,
    std::collections::HashMap,
    std::collections::HashSet,
    std::rc::Rc,
};

pub(crate) struct SubpathOptions {
    /// Options for the matching property name, including subpath-options for nested containers.
    /// If matched, these options apply exclusively; the `options_for_next_level` will not apply.
    subpath_options_by_name: HashMap<String, Rc<RefCell<SubpathOptions>>>,

    /// Options for nested containers under any array item, or any property not matching a property
    /// name in `subpath_options_by_name`.
    unnamed_subpath_options: Option<Rc<RefCell<SubpathOptions>>>,

    /// The options that override the default FormatOptions (those passed to `with_options()`) for
    /// the matched path.
    pub options: FormatOptions,

    /// A map of property names to priority values, for sorting properties at the matched path.
    property_name_priorities: HashMap<&'static str, usize>,
}

impl SubpathOptions {
    /// Properties without an explicit priority will be sorted after prioritized properties and
    /// retain their original order with respect to any other unpriorized properties.
    const NO_PRIORITY: usize = std::usize::MAX;

    pub fn new(default_options: &FormatOptions) -> Self {
        Self {
            subpath_options_by_name: HashMap::new(),
            unnamed_subpath_options: None,
            options: default_options.clone(),
            property_name_priorities: HashMap::new(),
        }
    }

    pub fn override_default_options(&mut self, path_options: &HashSet<PathOption>) {
        for path_option in path_options.iter() {
            use PathOption::*;
            match path_option {
                TrailingCommas(path_value) => self.options.trailing_commas = *path_value,
                CollapseContainersOfOne(path_value) => {
                    self.options.collapse_containers_of_one = *path_value
                }
                SortArrayItems(path_value) => self.options.sort_array_items = *path_value,
                PropertyNameOrder(property_names) => {
                    for (index, property_name) in property_names.iter().enumerate() {
                        self.property_name_priorities.insert(property_name, index);
                    }
                }
            }
        }
    }

    pub fn get_or_create_subpath_options(
        &mut self,
        path: &[&str],
        default_options: &FormatOptions,
    ) -> Rc<RefCell<SubpathOptions>> {
        let name_or_star = path[0];
        let remaining_path = &path[1..];
        let subpath_options_ref = if name_or_star == "*" {
            self.unnamed_subpath_options.as_ref()
        } else {
            self.subpath_options_by_name.get(name_or_star)
        };
        let subpath_options = match subpath_options_ref {
            Some(existing_options) => existing_options.clone(),
            None => {
                let new_options = Rc::new(RefCell::new(SubpathOptions::new(default_options)));
                if name_or_star == "*" {
                    self.unnamed_subpath_options = Some(new_options.clone());
                } else {
                    self.subpath_options_by_name
                        .insert(name_or_star.to_string(), new_options.clone());
                }
                new_options
            }
        };
        if remaining_path.len() == 0 {
            subpath_options
        } else {
            (*subpath_options.borrow_mut())
                .get_or_create_subpath_options(remaining_path, default_options)
        }
    }

    fn get_subpath_options(&self, path: &[&str]) -> Option<Rc<RefCell<SubpathOptions>>> {
        let name_or_star = path[0];
        let remaining_path = &path[1..];
        let subpath_options_ref = if name_or_star == "*" {
            self.unnamed_subpath_options.as_ref()
        } else {
            self.subpath_options_by_name.get(name_or_star)
        };
        if let Some(subpath_options) = subpath_options_ref {
            if remaining_path.len() == 0 {
                Some(subpath_options.clone())
            } else {
                (*subpath_options.borrow()).get_subpath_options(remaining_path)
            }
        } else {
            None
        }
    }

    fn get_options_for(&self, name_or_star: &str) -> Option<Rc<RefCell<SubpathOptions>>> {
        self.get_subpath_options(&[name_or_star])
    }

    pub fn get_property_priority(&self, property_name: &str) -> usize {
        match self.property_name_priorities.get(property_name) {
            Some(priority) => *priority,
            None => SubpathOptions::NO_PRIORITY,
        }
    }

    fn debug_format(
        &self,
        formatter: &mut std::fmt::Formatter<'_>,
        indent: &str,
    ) -> std::fmt::Result {
        writeln!(formatter, "{{")?;
        let next_indent = indent.to_owned() + "    ";
        writeln!(formatter, "{}options = {:?}", &next_indent, self.options)?;
        writeln!(
            formatter,
            "{}property_name_priorities = {:?}",
            &next_indent, self.property_name_priorities
        )?;
        if let Some(unnamed_subpath_options) = &self.unnamed_subpath_options {
            write!(formatter, "{}* = ", &next_indent)?;
            (*unnamed_subpath_options.borrow()).debug_format(formatter, &next_indent)?;
            writeln!(formatter)?;
        }
        for (property_name, subpath_options) in self.subpath_options_by_name.iter() {
            write!(formatter, "{}{} = ", &next_indent, property_name)?;
            (*subpath_options.borrow()).debug_format(formatter, &next_indent)?;
            writeln!(formatter)?;
        }
        writeln!(formatter, "{}}}", &indent)
    }
}

impl std::fmt::Debug for SubpathOptions {
    fn fmt(&self, formatter: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        self.debug_format(formatter, "")
    }
}

/// A JSON5 formatter that produces formatted JSON5 document content from a JSON5 `ParsedDocument`.
pub(crate) struct Formatter {
    /// The current depth of the partially-generated document while formatting. Each nexted array or
    /// object increases the depth by 1. After the formatted array or object has been generated, the
    /// depth decreases by 1.
    depth: usize,

    /// The next value to be written should be indented.
    pending_indent: bool,

    /// The UTF-8 bytes of the output document as it is being generated.
    bytes: Vec<u8>,

    /// The 1-based column number of the next character to be appended.
    column: usize,

    /// While generating the formatted document, these are the options to be applied at each nesting
    /// depth and path, from the document root to the object or array currently being generated. If
    /// the current path has no explicit options, the value at the top of the stack is None.
    subpath_options_stack: Vec<Option<Rc<RefCell<SubpathOptions>>>>,

    /// Options that alter how the formatter generates the formatted output. This instance of
    /// FormatOptions is a subset of the FormatOptions passed to the `with_options` constructor.
    /// The `options_by_path` are first removed, and then used to initialize the SubpathOptions
    /// hierarchy rooted at the `document_root_options_ref`.
    default_options: FormatOptions,
}

impl Formatter {
    /// Create and return a Formatter, with the given options to be applied to the
    /// [Json5Format::to_utf8()](struct.Json5Format.html#method.to_utf8) operation.
    pub fn new(
        default_options: FormatOptions,
        document_root_options_ref: Rc<RefCell<SubpathOptions>>,
    ) -> Self {
        Formatter {
            depth: 0,
            pending_indent: false,
            bytes: vec![],
            column: 1,
            subpath_options_stack: vec![Some(document_root_options_ref)],
            default_options,
        }
    }

    pub fn increase_indent(&mut self) -> Result<&mut Formatter, Error> {
        self.depth += 1;
        Ok(self)
    }

    pub fn decrease_indent(&mut self) -> Result<&mut Formatter, Error> {
        self.depth -= 1;
        Ok(self)
    }

    /// Appends the given string, indenting if required.
    pub fn append(&mut self, content: &str) -> Result<&mut Formatter, Error> {
        if self.pending_indent && !content.starts_with("\n") {
            let spaces = self.depth * self.default_options.indent_by;
            self.bytes.extend_from_slice(" ".repeat(spaces).as_bytes());
            self.column = spaces + 1;
            self.pending_indent = false;
        }
        if content.ends_with("\n") {
            self.column = 1;
            self.bytes.extend_from_slice(content.as_bytes());
        } else {
            let mut first = true;
            for line in content.lines() {
                if !first {
                    self.bytes.extend_from_slice("\n".as_bytes());
                    self.column = 1;
                }
                self.bytes.extend_from_slice(line.as_bytes());
                self.column += line.len();
                first = false;
            }
        }
        Ok(self)
    }

    pub fn append_newline(&mut self) -> Result<&mut Formatter, Error> {
        self.append("\n")
    }

    /// Outputs a newline (unless this is the first line), and sets the `pending_indent` flag to
    /// indicate the next non-blank line should be indented.
    pub fn start_next_line(&mut self) -> Result<&mut Formatter, Error> {
        if self.bytes.len() > 0 {
            self.append_newline()?;
        }
        self.pending_indent = true;
        Ok(self)
    }

    fn format_content<F>(&mut self, content_fn: F) -> Result<&mut Formatter, Error>
    where
        F: FnOnce(&mut Formatter) -> Result<&mut Formatter, Error>,
    {
        content_fn(self)
    }

    pub fn format_container<F>(
        &mut self,
        left_brace: &str,
        right_brace: &str,
        content_fn: F,
    ) -> Result<&mut Formatter, Error>
    where
        F: FnOnce(&mut Formatter) -> Result<&mut Formatter, Error>,
    {
        self.append(left_brace)?
            .increase_indent()?
            .format_content(content_fn)?
            .decrease_indent()?
            .append(right_brace)
    }

    fn format_comments_internal(
        &mut self,
        comments: &Vec<Comment>,
        leading_blank_line: bool,
    ) -> Result<&mut Formatter, Error> {
        let mut previous: Option<&Comment> = None;
        for comment in comments.iter() {
            match previous {
                Some(previous) => {
                    if comment.is_block() || previous.is_block() {
                        // Separate block comments and contiguous line comments.
                        // Use append_newline() instead of start_next_line() because block comment
                        // lines after the first line append their own indentation spaces.
                        self.append_newline()?;
                    }
                }
                None => {
                    if leading_blank_line {
                        self.start_next_line()?;
                    }
                }
            }
            comment.format(self)?;
            previous = Some(comment)
        }
        Ok(self)
    }

    pub fn format_comments(
        &mut self,
        comments: &Vec<Comment>,
        is_first: bool,
    ) -> Result<&mut Formatter, Error> {
        self.format_comments_internal(comments, !is_first)
    }

    pub fn format_trailing_comments(
        &mut self,
        comments: &Vec<Comment>,
    ) -> Result<&mut Formatter, Error> {
        self.format_comments_internal(comments, true)
    }

    pub fn get_current_subpath_options(&self) -> Option<&Rc<RefCell<SubpathOptions>>> {
        self.subpath_options_stack.last().unwrap().as_ref()
    }

    fn enter_scope(&mut self, name_or_star: &str) {
        let mut subpath_options_to_push = None;
        if let Some(current_subpath_options_ref) = self.get_current_subpath_options() {
            let current_subpath_options = &*current_subpath_options_ref.borrow();
            if let Some(next_subpath_options_ref) =
                current_subpath_options.get_options_for(name_or_star)
            {
                // SubpathOptions were explicitly provided for:
                //   * the given property name in the current object; or
                //   * all array items within the current array (as indicated by "*")
                subpath_options_to_push = Some(next_subpath_options_ref.clone());
            } else if name_or_star != "*" {
                if let Some(next_subpath_options_ref) = current_subpath_options.get_options_for("*")
                {
                    // `name_or_star` was a property name, and SubpathOptions for this path were
                    // _not_ explicitly defined for this name. In this case, a Subpath defined with
                    // "*" at this Subpath location, if provided, matches any property name in the
                    // current object (like a wildcard).
                    subpath_options_to_push = Some(next_subpath_options_ref.clone());
                }
            }
        }
        self.subpath_options_stack.push(subpath_options_to_push);
    }

    fn exit_scope(&mut self) {
        self.subpath_options_stack.pop();
    }

    fn format_scoped_value(
        &mut self,
        name: Option<&str>,
        value: &mut Value,
        is_first: bool,
        is_last: bool,
        container_has_pending_comments: bool,
    ) -> Result<&mut Formatter, Error> {
        let collapsed = is_first
            && is_last
            && value.is_primitive()
            && !value.has_comments()
            && !container_has_pending_comments
            && self.options_in_scope().collapse_containers_of_one;
        match name {
            // Above the enter_scope(...), the container's SubpathOptions affect formatting
            // and below, formatting is affected by named property or item SubpathOptions.
            //                 vvvvvvvvvvv
            Some(name) => self.enter_scope(name),
            None => self.enter_scope("*"),
        }
        if collapsed {
            self.append(" ")?;
        } else {
            if is_first {
                self.start_next_line()?;
            }
            self.format_comments(&value.comments().before_value(), is_first)?;
        }
        if let Some(name) = name {
            self.append(&format!("{}: ", name))?;
        }
        value.format(self)?;
        self.exit_scope();
        //   ^^^^^^^^^^
        // Named property or item SubpathOptions affect Formatting above exit_scope(...)
        // and below, formatting is affected by the container's SubpathOptions.
        if collapsed {
            self.append(" ")?;
        } else {
            self.append_comma(is_last)?
                .append_end_of_line_comment(value.comments().end_of_line())?
                .start_next_line()?;
        }
        Ok(self)
    }

    pub fn format_item(
        &mut self,
        item: &Rc<RefCell<Value>>,
        is_first: bool,
        is_last: bool,
        container_has_pending_comments: bool,
    ) -> Result<&mut Formatter, Error> {
        self.format_scoped_value(
            None,
            &mut *item.borrow_mut(),
            is_first,
            is_last,
            container_has_pending_comments,
        )
    }

    pub fn format_property(
        &mut self,
        property: &Property,
        is_first: bool,
        is_last: bool,
        container_has_pending_comments: bool,
    ) -> Result<&mut Formatter, Error> {
        self.format_scoped_value(
            Some(&property.name),
            &mut *property.value.borrow_mut(),
            is_first,
            is_last,
            container_has_pending_comments,
        )
    }

    pub fn options_in_scope(&self) -> FormatOptions {
        match self.get_current_subpath_options() {
            Some(subpath_options) => (*subpath_options.borrow()).options.clone(),
            None => self.default_options.clone(),
        }
    }

    fn append_comma(&mut self, is_last: bool) -> Result<&mut Formatter, Error> {
        if !is_last || self.options_in_scope().trailing_commas {
            self.append(",")?;
        }
        Ok(self)
    }

    /// Outputs the value's end-of-line comment. If the comment has multiple lines, the first line
    /// is written from the current position and all subsequent lines are written on their own line,
    /// left-aligned directly under the first comment.
    fn append_end_of_line_comment(
        &mut self,
        comment: &Option<String>,
    ) -> Result<&mut Formatter, Error> {
        if let Some(comment) = comment {
            let start_column = self.column;
            let mut first = true;
            for line in comment.lines() {
                if !first {
                    self.append_newline()?;
                    self.append(&" ".repeat(start_column - 1))?;
                }
                self.append(&format!(" //{}", line))?;
                first = false;
            }
        }
        Ok(self)
    }

    /// Formats the given document into the returned UTF8 byte buffer, consuming self.
    pub fn format(mut self, parsed_document: &ParsedDocument) -> Result<Vec<u8>, Error> {
        parsed_document.content.format_content(&mut self)?;
        Ok(self.bytes)
    }
}
