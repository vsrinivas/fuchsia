// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

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

    pub fn append(&mut self, content: &str) -> Result<&mut Formatter, Error> {
        if self.pending_indent && !content.starts_with("\n") {
            let spaces = self.depth * self.default_options.indent_by;
            self.bytes.extend_from_slice(" ".repeat(spaces).as_bytes());
            self.pending_indent = false;
        }
        self.bytes.extend_from_slice(content.as_bytes());
        Ok(self)
    }

    pub fn append_newline(&mut self) -> Result<&mut Formatter, Error> {
        self.append("\n")
    }

    pub fn start_next_line(&mut self) -> Result<&mut Formatter, Error> {
        if self.bytes.len() > 0 {
            self.append_newline()?;
        }
        self.pending_indent = true;
        Ok(self)
    }

    pub fn format_content<F>(&mut self, content_fn: F) -> Result<&mut Formatter, Error>
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
            && !value.meta().has_comments()
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
            self.format_comments(&value.meta().get_comments(), is_first)?;
        }
        if let Some(name) = name {
            self.append(&format!("{}: ", name))?;
        }
        value.meta().format(self)?;
        self.exit_scope();
        //   ^^^^^^^^^^
        // Named property or item SubpathOptions affect Formatting above exit_scope(...)
        // and below, formatting is affected by the container's SubpathOptions.
        if collapsed {
            self.append(" ")?;
        } else {
            self.append_comma(is_last)?
                .append_end_of_line_comment(value.meta().get_end_of_line_comment())?
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

    fn append_end_of_line_comment(
        &mut self,
        comment: &Option<String>,
    ) -> Result<&mut Formatter, Error> {
        if let Some(comment) = comment {
            self.append(&format!(" //{}", comment))?;
        }
        Ok(self)
    }

    /// Formats the given document into the returned UTF8 byte buffer, consuming self.
    pub fn format(mut self, parsed_document: &ParsedDocument) -> Result<Vec<u8>, Error> {
        parsed_document.content.format_content(&mut self)?;
        Ok(self.bytes)
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        crate::{test_error, Json5Format},
        maplit::hashmap,
        maplit::hashset,
    };

    struct FormatTest<'a> {
        options: Option<FormatOptions>,
        input: &'a str,
        error: Option<&'a str>,
        expected: &'a str,
    }

    impl<'a> Default for FormatTest<'a> {
        fn default() -> Self {
            FormatTest { options: None, input: "", error: None, expected: "" }
        }
    }

    fn try_test_format(test: FormatTest<'_>) -> Result<(), Error> {
        let result = match ParsedDocument::from_str(test.input, None) {
            Ok(parsed_document) => {
                let format = match test.options {
                    Some(options) => Json5Format::with_options(options)?,
                    None => Json5Format::new()?,
                };
                format.to_utf8(&parsed_document)
            }
            Err(actual_error) => Err(actual_error),
        };
        match result {
            Ok(bytes) => {
                let actual_formatted_document = std::str::from_utf8(&bytes).unwrap();
                match test.error {
                    Some(expected_error) => {
                        println!("Unexpected formatted result:");
                        println!("===========================");
                        println!("{}", actual_formatted_document);
                        println!("===========================");
                        println!("Expected error: {}", expected_error);
                        Err(test_error!(format!(
                            "Unexpected 'Ok()' result.\n expected: '{}'",
                            expected_error
                        )))
                    }
                    None => {
                        if actual_formatted_document == test.expected {
                            Ok(())
                        } else {
                            println!("expected:");
                            println!("========");
                            println!("{}", test.expected);
                            println!("========");
                            println!("actual:");
                            println!("======");
                            println!("{}", actual_formatted_document);
                            println!("======");
                            Err(test_error!(format!(
                                "Actual formatted document did not match expected."
                            )))
                        }
                    }
                }
            }
            Err(actual_error) => match test.error {
                Some(expected_error) => {
                    let actual_error = format!("{}", actual_error);
                    if expected_error == actual_error {
                        Ok(())
                    } else {
                        println!("expected: {}", expected_error);
                        println!("  actual: {}", actual_error);
                        Err(test_error!("Actual error did not match expected error."))
                    }
                }
                None => Err(actual_error),
            },
        }
    }

    fn test_format(test: FormatTest<'_>) -> Result<(), Error> {
        try_test_format(test).map_err(|e| {
            println!("{}", e);
            e
        })
    }

    #[test]
    fn test_format_simple_cml() {
        test_format(FormatTest {
            input: r##"{ "program": {} }"##,
            expected: r##"{
    program: {},
}
"##,
            ..Default::default()
        })
        .unwrap()
    }

    #[test]
    fn test_last_scope_is_array() {
        test_format(FormatTest {
            input: r##"{
    program: {},
    expose: [
        {
        }

     /* and this */
    ]
}   // line comment on primary object

        // line comment at the end of the document
        // second line comment

    /* block comment at the end of the document
     * block comment continues.
     * end of block comment at end of doc */
"##,
            expected: r##"{
    program: {},
    expose: [
        {},

        /* and this */
    ],
} // line comment on primary object

// line comment at the end of the document
// second line comment

/* block comment at the end of the document
 * block comment continues.
 * end of block comment at end of doc */
"##,
            ..Default::default()
        })
        .unwrap()
    }

    #[test]
    fn test_comment_block() {
        test_format(FormatTest {
            input: r##"// Copyright or other header
    // goes here
{
    program: {},
    expose: [
    /*
    what happens
    with this
    */
      /*
    what happens
    with this
      */
      /*
       what happens
       with this
      */
      /*
        what happens
        with this
      */
      /* what happens
         with this
         */
      /* what happens
         with this
         and this */
         {
         }

         /* and this */
    ]
    }
        // and end of
        // the doc comment"##,
            expected: r##"// Copyright or other header
// goes here
{
    program: {},
    expose: [
        /*
        what happens
        with this
        */

        /*
    what happens
    with this
      */

        /*
         what happens
         with this
        */

        /*
          what happens
          with this
        */

        /* what happens
           with this
        */

        /* what happens
           with this
           and this */
        {},

        /* and this */
    ],
}

// and end of
// the doc comment
"##,
            ..Default::default()
        })
        .unwrap()
    }

    #[test]
    fn test_breaks_between_line_comments() {
        test_format(FormatTest {
            input: r##"// Copyright or other header
    // goes here

// Another comment block
// separate from the copyright block.
{

    /// doc comment
    /// is here
    program: {},

    /// another doc comment
        /* and block comment */
    /// and doc comment



    /// and multiple blank lines were above this line comment,
    /// but replaced by one.

    /// more than
    /// two contiguous
    /// line comments
    /// are
    /// here
    ///
    /// including empty line comments

    expose: [ // inside array so not end of line comment
// comment block
        // is here

//comment block
// is here 2

        //comment block
        // is here 3

        // and one more

/* and a block comment
        */
    ],
    use: // line comment for "use"
    [
    ],
    offer: [
    ], // end of line comment for "offer"
}
        // and end of

        // the doc comment
        // was another break"##,
            expected: r##"// Copyright or other header
// goes here

// Another comment block
// separate from the copyright block.
{
    /// doc comment
    /// is here
    program: {},

    /// another doc comment

    /* and block comment */

    /// and doc comment

    /// and multiple blank lines were above this line comment,
    /// but replaced by one.

    /// more than
    /// two contiguous
    /// line comments
    /// are
    /// here
    ///
    /// including empty line comments
    expose: [
        // inside array so not end of line comment
        // comment block
        // is here

        //comment block
        // is here 2

        //comment block
        // is here 3

        // and one more

        /* and a block comment
        */
    ],

    // line comment for "use"
    use: [],
    offer: [], // end of line comment for "offer"
}

// and end of

// the doc comment
// was another break
"##,
            ..Default::default()
        })
        .unwrap()
    }

    #[test]
    fn test_format_sort_and_align_block_comment() {
        test_format(FormatTest {
            options: Some(FormatOptions { sort_array_items: true, ..Default::default() }),
            input: r##"{
    "program": {
        "binary": "bin/session_manager"
    },
    "use": [
        { "runner": "elf" },
        {
            // The Realm service allows session_manager to start components.
            "protocol": "/svc/fuchsia.sys2.Realm",
            "from": "framework",
        },
        {
        /* indented block
           comment:
             * is here
             * ok
        */
            "protocol": [
                "/svc/fuchsia.logger.LogSink",
                "/svc/fuchsia.cobalt.LoggerFactory",
            ],
            "from": "realm",
        },
    ],
}
"##,
            expected: r##"{
    program: {
        binary: "bin/session_manager",
    },
    use: [
        {
            runner: "elf",
        },
        {
            // The Realm service allows session_manager to start components.
            protocol: "/svc/fuchsia.sys2.Realm",
            from: "framework",
        },
        {
            /* indented block
               comment:
                 * is here
                 * ok
            */
            protocol: [
                "/svc/fuchsia.cobalt.LoggerFactory",
                "/svc/fuchsia.logger.LogSink",
            ],
            from: "realm",
        },
    ],
}
"##,
            ..Default::default()
        })
        .unwrap()
    }

    #[test]
    fn test_property_name_formatting() {
        test_format(FormatTest {
            input: r##"{
    unquotedName: 1,
    $_is_ok_$: 2,
    $10million: 3,
    _10_9_8___: 4,
    "remove_quotes_$_123": 5,
    "keep quotes": 6,
    "multi \
line \
is \
valid": 7,
    "3.14159": "pi",
    "with 'quotes'": 9,
    'with "quotes"': 10,
}
"##,
            expected: r##"{
    unquotedName: 1,
    $_is_ok_$: 2,
    $10million: 3,
    _10_9_8___: 4,
    remove_quotes_$_123: 5,
    "keep quotes": 6,
    "multi \
line \
is \
valid": 7,
    "3.14159": "pi",
    "with 'quotes'": 9,
    'with "quotes"': 10,
}
"##,
            ..Default::default()
        })
        .unwrap()
    }

    #[test]
    fn test_parse_error_missing_property_value() {
        test_format(FormatTest {
            input: r##"{
    property: {
        sub_property_1: "value",
        sub_property_2: ,
    }
}
"##,
            error: Some(
                "Parse error: 4:25: Property 'sub_property_2' must have a value before the next \
                 comma-separated property:
        sub_property_2: ,
                        ^",
            ),
            ..Default::default()
        })
        .unwrap();
    }

    #[test]
    fn test_parse_error_missing_property_value_when_closing_object() {
        test_format(FormatTest {
            input: r##"{
    property: {
        sub_property_1: "value",
        sub_property_2:
    }
}
"##,
            error: Some(
                "Parse error: 5:5: Property 'sub_property_2' must have a value before closing an \
                 object:
    }
    ^",
            ),
            ..Default::default()
        })
        .unwrap();
    }

    #[test]
    fn test_parse_error_incomplete_property() {
        test_format(FormatTest {
            input: r##"{
    property: {
        sub_property_1: "value1"
        sub_property_2: "value2",
    }
}
"##,
            error: Some(
                r#"Parse error: 4:9: Properties must be separated by a comma:
        sub_property_2: "value2",
        ^~~~~~~~~~~~~~~"#,
            ),
            ..Default::default()
        })
        .unwrap();

        test_format(FormatTest {
            input: r##"{
    property: {
        sub_property_1:
        sub_property_2: "value2",
    }
}
"##,
            error: Some(
                r#"Parse error: 4:9: Properties must be separated by a comma:
        sub_property_2: "value2",
        ^~~~~~~~~~~~~~~"#,
            ),
            ..Default::default()
        })
        .unwrap();

        test_format(FormatTest {
            input: r##"{
    property: {
        sub_property_1: ,
        sub_property_2: "value2",
    }
}
"##,
            error: Some(
                "Parse error: 3:25: Property 'sub_property_1' must have a value before the next \
                 comma-separated property:
        sub_property_1: ,
                        ^",
            ),
            ..Default::default()
        })
        .unwrap();
    }

    #[test]
    fn test_parse_error_property_name_when_array_value_is_expected() {
        test_format(FormatTest {
            input: r##"{
    property: [
        "item1",
        sub_property_1: "value",
    }
}
"##,
            error: Some(r#"Parse error: 4:9: Invalid Object token found while parsing an Array of 1 item (mismatched braces?):
        sub_property_1: "value",
        ^~~~~~~~~~~~~~~"#),
            ..Default::default()
        })
        .unwrap();
    }

    #[test]
    fn test_parse_error_bad_non_string_primitive() {
        test_format(FormatTest {
            input: r##"{
    non_string_literals: [
        null,
        true,
        false,

        12345,
        12345.67890,
        12345.,
        .67890,
        1234e5678,
        1234E5678,
        1234e+5678,
        1234E+5678,
        1234e-5678,
        1234E-5678,
        0xabc123ef,
        0Xabc123EF,
        NaN,
        Infinity,

        -12345,
        -12345.67890,
        -12345.,
        -.67890,
        -1234e5678,
        -1234E5678,
        -1234e+5678,
        -1234E+5678,
        -1234e-5678,
        -1234E-5678,
        -0xabc123ef,
        -0Xabc123EF,
        -NaN,
        -Infinity,

        +12345,
        +12345.67890,
        +12345.,
        +.67890,
        +1234e5678,
        +1234E5678,
        +1234e+5678,
        +1234E+5678,
        +1234e-5678,
        +1234E-5678,
        +0xabc123ef,
        +0Xabc123EF,
        +NaN,
        +Infinity,

        123def,
        0x123def,
    ]
}
"##,
            error: Some(
                "Parse error: 52:9: Unexpected token:
        123def,
        ^",
            ),
            ..Default::default()
        })
        .unwrap();
    }

    #[test]
    fn test_parse_error_expected_object() {
        test_format(FormatTest {
            input: r##"{
    property: [}
}
"##,
            error: Some(r#"Parse error: 2:16: Invalid Object token found while parsing an Array of 0 items (mismatched braces?):
    property: [}
               ^"#),
            ..Default::default()
        })
        .unwrap();
    }

    #[test]
    fn test_parse_error_expected_array() {
        test_format(FormatTest {
            input: r##"{
    property: {]
}
"##,
            error: Some(r#"Parse error: 2:16: Invalid Array token found while parsing an Object of 0 properties (mismatched braces?):
    property: {]
               ^"#),
            ..Default::default()
        })
        .unwrap();
    }

    #[test]
    fn test_parse_error_mismatched_braces() {
        test_format(FormatTest {
            input: r##"{
    property_1: "value1",
    property_2: "value2","##,
            error: Some(
                r#"Parse error: 3:25: Mismatched braces in the document:
    property_2: "value2",
                        ^"#,
            ),
            ..Default::default()
        })
        .unwrap();
    }

    #[test]
    fn test_property_name_separator_missing() {
        test_format(FormatTest {
            input: r##"{
    property_1 "value1",
}
"##,
            error: Some(
                r#"Parse error: 2:5: Unexpected token:
    property_1 "value1",
    ^"#,
            ),
            ..Default::default()
        })
        .unwrap();
    }

    #[test]
    fn test_parse_error_quoted_property_name_separator_missing() {
        test_format(FormatTest {
            input: r##"{
    "property_1" "value1",
}
"##,
            error: Some(
                r#"Parse error: 2:17: Property name separator (:) missing:
    "property_1" "value1",
                ^"#,
            ),
            ..Default::default()
        })
        .unwrap();
    }

    #[test]
    fn test_parse_error_extra_comma_between_properties() {
        test_format(FormatTest {
            input: r##"{
    property_1: "value1",
    ,
    property_2: "value2",
}
"##,
            error: Some(
                "Parse error: 3:5: Unexpected comma without a preceding property:
    ,
    ^",
            ),
            ..Default::default()
        })
        .unwrap();
    }

    #[test]
    fn test_parse_error_comma_before_first_property() {
        test_format(FormatTest {
            input: r##"{
    ,
    property_1: "value1",
    property_2: "value2",
}
"##,
            error: Some(
                "Parse error: 2:5: Unexpected comma without a preceding property:
    ,
    ^",
            ),
            ..Default::default()
        })
        .unwrap();
    }

    #[test]
    fn test_parse_error_extra_comma_between_array_items() {
        test_format(FormatTest {
            input: r##"[
    "value1",
    ,
    "value2",
]"##,
            error: Some(
                "Parse error: 3:5: Unexpected comma without a preceding array item value:
    ,
    ^",
            ),
            ..Default::default()
        })
        .unwrap();
    }

    #[test]
    fn test_parse_error_comma_before_first_array_item() {
        test_format(FormatTest {
            input: r##"[
    ,
    "value1",
    "value2",
]"##,
            error: Some(
                "Parse error: 2:5: Unexpected comma without a preceding array item value:
    ,
    ^",
            ),
            ..Default::default()
        })
        .unwrap();
    }

    #[test]
    fn test_parse_error_quoted_property_name_and_comma_looks_like_a_value() {
        test_format(FormatTest {
            input: r##"{
    property_1: "value1",
    "value2",
}
"##,
            error: Some(
                r#"Parse error: 3:13: Property name separator (:) missing:
    "value2",
            ^"#,
            ),
            ..Default::default()
        })
        .unwrap();
    }

    #[test]
    fn test_parse_error_value_without_property_name() {
        test_format(FormatTest {
            input: r##"{
    property_1: "value1",
    false,
}
"##,
            error: Some(
                "Parse error: 3:5: Object values require property names:
    false,
    ^~~~~",
            ),
            ..Default::default()
        })
        .unwrap();
    }

    #[test]
    fn test_parse_error_unclosed_string() {
        test_format(FormatTest {
            input: r##"{
    property: "bad quotes',
}
"##,
            error: Some(
                r#"Parse error: 2:16: Unclosed string:
    property: "bad quotes',
               ^"#,
            ),
            ..Default::default()
        })
        .unwrap();
    }

    #[test]
    fn test_parse_error_not_json() {
        test_format(FormatTest {
            input: r##"
# Fuchsia

Pink + Purple == Fuchsia (a new operating system)

## How can I build and run Fuchsia?

See [Getting Started](https://fuchsia.dev/fuchsia-src/getting_started.md).

## Where can I learn more about Fuchsia?

See [fuchsia.dev](https://fuchsia.dev).
"##,
            error: Some(
                r#"Parse error: 2:1: Unexpected token:
# Fuchsia
^"#,
            ),
            ..Default::default()
        })
        .unwrap();
    }

    #[test]
    fn test_options() {
        let options = FormatOptions { ..Default::default() };
        assert_eq!(options.indent_by, 4);
        assert_eq!(options.trailing_commas, true);
        assert_eq!(options.collapse_containers_of_one, false);
        assert_eq!(options.sort_array_items, false);

        let options = FormatOptions {
            indent_by: 2,
            trailing_commas: false,
            collapse_containers_of_one: true,
            sort_array_items: true,
            options_by_path: hashmap! {
                "/*" => hashset! {
                    PathOption::PropertyNameOrder(vec![
                        "program",
                        "use",
                        "expose",
                        "offer",
                        "children",
                        "collections",
                        "storage",
                        "facets",
                        "runners",
                        "resolvers",
                        "environments",
                    ]),
                },
                "/*/use" => hashset! {
                    PathOption::TrailingCommas(false),
                    PathOption::CollapseContainersOfOne(false),
                    PathOption::SortArrayItems(true),
                    PathOption::PropertyNameOrder(vec![
                        "name",
                        "url",
                        "startup",
                        "environment",
                        "durability",
                        "service",
                        "protocol",
                        "directory",
                        "storage",
                        "runner",
                        "resolver",
                        "to",
                        "from",
                        "as",
                        "rights",
                        "subdir",
                        "path",
                        "dependency",
                    ]),
                },
                "/*/use/service" => hashset! {
                    PathOption::SortArrayItems(true),
                },
            },
            ..Default::default()
        };

        assert_eq!(options.indent_by, 2);
        assert_eq!(options.trailing_commas, false);
        assert_eq!(options.collapse_containers_of_one, true);
        assert_eq!(options.sort_array_items, true);

        let path_options = options
            .options_by_path
            .get("/*/use")
            .expect("Expected to find path options for the given path");
        match path_options
            .get(&PathOption::TrailingCommas(true))
            .expect("Expected to find a PathOption::TrailingCommas setting")
        {
            PathOption::TrailingCommas(trailing_commas) => assert_eq!(*trailing_commas, false),
            _ => panic!("PathOption enum as key should return a value of the same type"),
        };
        match path_options
            .get(&PathOption::CollapseContainersOfOne(true))
            .expect("Expected to find a PathOption::CollapseContainersOfOne setting")
        {
            PathOption::CollapseContainersOfOne(collapsed_container_of_one) => {
                assert_eq!(*collapsed_container_of_one, false)
            }
            _ => panic!("PathOption enum as key should return a value of the same type"),
        };
        match path_options
            .get(&PathOption::SortArrayItems(true))
            .expect("Expected to find a PathOption::SortArrayItems setting")
        {
            PathOption::SortArrayItems(sort_array_items) => assert_eq!(*sort_array_items, true),
            _ => panic!("PathOption enum as key should return a value of the same type"),
        };
        match path_options
            .get(&PathOption::PropertyNameOrder(vec![]))
            .expect("Expected to find a PathOption::PropertyNameOrder setting")
        {
            PathOption::PropertyNameOrder(property_names) => assert_eq!(property_names[1], "url"),
            _ => panic!("PathOption enum as key should return a value of the same type"),
        };
    }

    #[test]
    fn test_duplicated_key_in_subpath_options_is_ignored() {
        let options = FormatOptions {
            options_by_path: hashmap! {
                "/*/use" => hashset! {
                    PathOption::TrailingCommas(false),
                    PathOption::CollapseContainersOfOne(false),
                    PathOption::SortArrayItems(true),
                    PathOption::PropertyNameOrder(vec![
                        "name",
                        "url",
                        "startup",
                        "environment",
                        "durability",
                        "service",
                        "protocol",
                        "directory",
                        "storage",
                        "runner",
                        "resolver",
                        "to",
                        "from",
                        "as",
                        "rights",
                        "subdir",
                        "path",
                        "dependency",
                    ]),
                    PathOption::SortArrayItems(false),
                },
            },
            ..Default::default()
        };

        match options.options_by_path.get("/*/use") {
            Some(path_options) => {
                match path_options.get(&PathOption::TrailingCommas(true)) {
                    Some(path_option) => match path_option {
                        PathOption::TrailingCommas(trailing_commas) => {
                            assert_eq!(*trailing_commas, false);
                        }
                        _ => {
                            panic!("PathOption enum as key should return a value of the same type")
                        }
                    },
                    None => panic!("Expected to find a PathOption::TrailingCommas setting"),
                }
                match path_options.get(&PathOption::CollapseContainersOfOne(true)) {
                    Some(path_option) => match path_option {
                        PathOption::CollapseContainersOfOne(collapsed_container_of_one) => {
                            assert_eq!(*collapsed_container_of_one, false);
                        }
                        _ => {
                            panic!("PathOption enum as key should return a value of the same type")
                        }
                    },
                    None => {
                        panic!("Expected to find a PathOption::CollapseContainersOfOne setting")
                    }
                }
                match path_options.get(&PathOption::SortArrayItems(true)) {
                    Some(path_option) => match path_option {
                        PathOption::SortArrayItems(sort_array_items) => {
                            assert_eq!(*sort_array_items, true);
                        }
                        _ => {
                            panic!("PathOption enum as key should return a value of the same type")
                        }
                    },
                    None => panic!("Expected to find a PathOption::SortArrayItems setting"),
                }
                match path_options.get(&PathOption::PropertyNameOrder(vec![])) {
                    Some(path_option) => match path_option {
                        PathOption::PropertyNameOrder(property_names) => {
                            assert_eq!(property_names[1], "url");
                        }
                        _ => {
                            panic!("PathOption enum as key should return a value of the same type")
                        }
                    },
                    None => panic!("Expected to find a PathOption::PropertyNamePriorities setting"),
                }
            }
            None => panic!("Expected to find path options for the given path"),
        }
    }

    #[test]
    fn test_format_options() {
        test_format(FormatTest {
            options: Some(FormatOptions {
                collapse_containers_of_one: true,
                sort_array_items: true, // but use options_by_path to turn this off for program args
                options_by_path: hashmap! {
                    "/*" => hashset! {
                        PathOption::PropertyNameOrder(vec![
                            "program",
                            "children",
                            "collections",
                            "use",
                            "offer",
                            "expose",
                            "resolvers",
                            "runners",
                            "storage",
                            "environments",
                            "facets",
                        ])
                    },
                    "/*/program" => hashset! {
                        PathOption::CollapseContainersOfOne(false),
                        PathOption::PropertyNameOrder(vec![
                            "binary",
                            "args",
                        ])
                    },
                    "/*/program/args" => hashset! {
                        PathOption::SortArrayItems(false),
                    },
                    "/*/*/*" => hashset! {
                        PathOption::PropertyNameOrder(vec![
                            "name",
                            "url",
                            "startup",
                            "environment",
                            "durability",
                            "service",
                            "protocol",
                            "directory",
                            "resolver",
                            "runner",
                            "storage",
                            "from",
                            "as",
                            "to",
                            "rights",
                            "path",
                            "subdir",
                            "event",
                            "dependency",
                            "extends",
                            "resolvers",
                        ])
                    },
                },
                ..Default::default()
            }),
            input: r##"{
    offer: [
        {
            runner: "elf",
        },
        {
            from: "framework",
            to: "#elements",
            protocol: "/svc/fuchsia.sys2.Realm",
        },
        {
            to: "#elements",
            protocol: [
                "/svc/fuchsia.logger.LogSink",
                "/svc/fuchsia.cobalt.LoggerFactory",
            ],
            from: "realm",
        },
    ],
    collections: [
        "elements",
    ],
    use: [
        {
            runner: "elf",
        },
        {
            protocol: "/svc/fuchsia.sys2.Realm",
            from: "framework",
        },
        {
            to: "#elements",
            from: "realm",
            protocol: [
                "/svc/fuchsia.logger.LogSink",
                "/svc/fuchsia.cobalt.LoggerFactory",
            ],
        },
    ],
    children: [
    ],
    program: {
        binary: "bin/session_manager",
    },
}
"##,
            expected: r##"{
    program: {
        binary: "bin/session_manager",
    },
    children: [],
    collections: [ "elements" ],
    use: [
        { runner: "elf" },
        {
            protocol: "/svc/fuchsia.sys2.Realm",
            from: "framework",
        },
        {
            protocol: [
                "/svc/fuchsia.cobalt.LoggerFactory",
                "/svc/fuchsia.logger.LogSink",
            ],
            from: "realm",
            to: "#elements",
        },
    ],
    offer: [
        { runner: "elf" },
        {
            protocol: "/svc/fuchsia.sys2.Realm",
            from: "framework",
            to: "#elements",
        },
        {
            protocol: [
                "/svc/fuchsia.cobalt.LoggerFactory",
                "/svc/fuchsia.logger.LogSink",
            ],
            from: "realm",
            to: "#elements",
        },
    ],
}
"##,
            ..Default::default()
        })
        .unwrap();
    }

    #[test]
    fn test_no_trailing_commas() {
        test_format(FormatTest {
            options: Some(FormatOptions { trailing_commas: false, ..Default::default() }),
            input: r##"{
    offer: [
        {
            runner: "elf",
        },
        {
            from: "framework",
            to: "#elements",
            protocol: "/svc/fuchsia.sys2.Realm",
        },
        {
            to: "#elements",
            protocol: [
                "/svc/fuchsia.logger.LogSink",
                "/svc/fuchsia.cobalt.LoggerFactory",
            ],
            from: "realm",
        },
    ],
    collections: [
        "elements",
    ],
    use: [
        {
            runner: "elf",
        },
        {
            protocol: "/svc/fuchsia.sys2.Realm",
            from: "framework",
        },
        {
            from: "realm",
            to: "#elements",
            protocol: [
                "/svc/fuchsia.logger.LogSink",
                "/svc/fuchsia.cobalt.LoggerFactory",
            ],
        },
    ],
    children: [
    ],
    program: {
        binary: "bin/session_manager",
    },
}
"##,
            expected: r##"{
    offer: [
        {
            runner: "elf"
        },
        {
            from: "framework",
            to: "#elements",
            protocol: "/svc/fuchsia.sys2.Realm"
        },
        {
            to: "#elements",
            protocol: [
                "/svc/fuchsia.logger.LogSink",
                "/svc/fuchsia.cobalt.LoggerFactory"
            ],
            from: "realm"
        }
    ],
    collections: [
        "elements"
    ],
    use: [
        {
            runner: "elf"
        },
        {
            protocol: "/svc/fuchsia.sys2.Realm",
            from: "framework"
        },
        {
            from: "realm",
            to: "#elements",
            protocol: [
                "/svc/fuchsia.logger.LogSink",
                "/svc/fuchsia.cobalt.LoggerFactory"
            ]
        }
    ],
    children: [],
    program: {
        binary: "bin/session_manager"
    }
}
"##,
            ..Default::default()
        })
        .unwrap();
    }

    #[test]
    fn test_collapse_containers_of_one() {
        test_format(FormatTest {
            options: Some(FormatOptions { collapse_containers_of_one: true, ..Default::default() }),
            input: r##"{
    offer: [
        {
            runner: "elf",
        },
        {
            from: "framework",
            to: "#elements",
            protocol: "/svc/fuchsia.sys2.Realm",
        },
        {
            to: "#elements",
            protocol: [
                "/svc/fuchsia.logger.LogSink",
                "/svc/fuchsia.cobalt.LoggerFactory",
            ],
            from: "realm",
        },
    ],
    collections: [
        "elements",
    ],
    use: [
        {
            runner: "elf",
        },
        {
            protocol: "/svc/fuchsia.sys2.Realm",
            from: "framework",
        },
        {
            from: "realm",
            to: "#elements",
            protocol: [
                "/svc/fuchsia.logger.LogSink",
                "/svc/fuchsia.cobalt.LoggerFactory",
            ],
        },
    ],
    children: [
    ],
    program: {
        binary: "bin/session_manager",
    },
}
"##,
            expected: r##"{
    offer: [
        { runner: "elf" },
        {
            from: "framework",
            to: "#elements",
            protocol: "/svc/fuchsia.sys2.Realm",
        },
        {
            to: "#elements",
            protocol: [
                "/svc/fuchsia.logger.LogSink",
                "/svc/fuchsia.cobalt.LoggerFactory",
            ],
            from: "realm",
        },
    ],
    collections: [ "elements" ],
    use: [
        { runner: "elf" },
        {
            protocol: "/svc/fuchsia.sys2.Realm",
            from: "framework",
        },
        {
            from: "realm",
            to: "#elements",
            protocol: [
                "/svc/fuchsia.logger.LogSink",
                "/svc/fuchsia.cobalt.LoggerFactory",
            ],
        },
    ],
    children: [],
    program: { binary: "bin/session_manager" },
}
"##,
            ..Default::default()
        })
        .unwrap();
    }

    #[test]
    fn test_validate_example_in_documentation() {
        test_format(FormatTest {
            options: Some(FormatOptions {
                options_by_path: hashmap! {
                    "/*" => hashset! {
                        PathOption::PropertyNameOrder(vec![
                            "name",
                            "address",
                            "contact_options",
                        ]),
                    },
                    "/*/name" => hashset! {
                        PathOption::PropertyNameOrder(vec![
                            "first",
                            "middle",
                            "last",
                            "suffix",
                        ]),
                    },
                    "/*/*/*" => hashset! {
                        PathOption::PropertyNameOrder(vec![
                            "work",
                            "home",
                            "other",
                        ]),
                    },
                    "/*/*/*/work" => hashset! {
                        PathOption::PropertyNameOrder(vec![
                            "phone",
                            "email",
                        ]),
                    },
                },
                ..Default::default()
            }),
            input: r##"{
    name: {
        last: "Smith",
        first: "John",
        middle: "Jacob",
    },
    address: {
        city: "Anytown",
        country: "USA",
        state: "New York",
        street: "101 Main Street",
    },
    contact_options: [
        {
            other: {
                email: "volunteering@serviceprojectsrus.org",
            },
            home: {
                email: "jj@notreallygmail.com",
                phone: "212-555-4321",
            },
        },
        {
            home: {
                email: "john.smith@notreallygmail.com",
                phone: "212-555-2222",
            },
            work: {
                email: "john.j.smith@worksforme.gov",
                phone: "212-555-1234",
            },
        },
    ],
}
"##,
            expected: r##"{
    name: {
        first: "John",
        middle: "Jacob",
        last: "Smith",
    },
    address: {
        city: "Anytown",
        country: "USA",
        state: "New York",
        street: "101 Main Street",
    },
    contact_options: [
        {
            home: {
                email: "jj@notreallygmail.com",
                phone: "212-555-4321",
            },
            other: {
                email: "volunteering@serviceprojectsrus.org",
            },
        },
        {
            work: {
                phone: "212-555-1234",
                email: "john.j.smith@worksforme.gov",
            },
            home: {
                email: "john.smith@notreallygmail.com",
                phone: "212-555-2222",
            },
        },
    ],
}
"##,
            ..Default::default()
        })
        .unwrap();
    }
}
