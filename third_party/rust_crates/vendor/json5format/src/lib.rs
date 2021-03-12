// Copyright (c) 2020 Google LLC All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

//! A stylized formatter for [JSON5](https://json5.org) ("JSON for Humans") documents.
//!
//! The intent of this formatter is to rewrite a given valid JSON5 document, restructuring the
//! output (if required) to conform to a consistent style.
//!
//! The resulting document should preserve all data precision, data format representations, and
//! semantic intent. Readability should be maintained, if not improved by the consistency within and
//! across documents.
//!
//! Most importantly, all JSON5 comments should be preserved, maintaining the
//! positional relationship with the JSON5 data elements they were intended to document.
//!
//! # Example
//!
//! ```rust
//!   use json5format::*;
//!   use maplit::hashmap;
//!   use maplit::hashset;
//!
//!   let json5=r##"{
//!       "name": {
//!           "last": "Smith",
//!           "first": "John",
//!           "middle": "Jacob"
//!       },
//!       "children": [
//!           "Buffy",
//!           "Biff",
//!           "Balto"
//!       ],
//!       // Consider adding a note field to the `other` contact option
//!       "contact_options": [
//!           {
//!               "home": {
//!                   "email": "jj@notreallygmail.com",   // This was the original user id.
//!                                                       // Now user id's are hash values.
//!                   "phone": "212-555-4321"
//!               },
//!               "other": {
//!                   "email": "volunteering@serviceprojectsrus.org"
//!               },
//!               "work": {
//!                   "phone": "212-555-1234",
//!                   "email": "john.j.smith@worksforme.gov"
//!               }
//!           }
//!       ],
//!       "address": {
//!           "city": "Anytown",
//!           "country": "USA",
//!           "state": "New York",
//!           "street": "101 Main Street"
//!           /* Update schema to support multiple addresses:
//!              "work": {
//!                  "city": "Anytown",
//!                  "country": "USA",
//!                  "state": "New York",
//!                  "street": "101 Main Street"
//!              }
//!           */
//!       }
//!   }
//!   "##;
//!
//!   let options = FormatOptions {
//!       indent_by: 2,
//!       collapse_containers_of_one: true,
//!       options_by_path: hashmap! {
//!           "/*" => hashset! {
//!               PathOption::PropertyNameOrder(vec![
//!                   "name",
//!                   "address",
//!                   "contact_options",
//!               ]),
//!           },
//!           "/*/name" => hashset! {
//!               PathOption::PropertyNameOrder(vec![
//!                   "first",
//!                   "middle",
//!                   "last",
//!                   "suffix",
//!               ]),
//!           },
//!           "/*/children" => hashset! {
//!               PathOption::SortArrayItems(true),
//!           },
//!           "/*/*/*" => hashset! {
//!               PathOption::PropertyNameOrder(vec![
//!                   "work",
//!                   "home",
//!                   "other",
//!               ]),
//!           },
//!           "/*/*/*/*" => hashset! {
//!               PathOption::PropertyNameOrder(vec![
//!                   "phone",
//!                   "email",
//!               ]),
//!           },
//!       },
//!       ..Default::default()
//!   };
//!
//!   let filename = "new_contact.json5".to_string();
//!
//!   let format = Json5Format::with_options(options)?;
//!   let parsed_document = ParsedDocument::from_str(&json5, Some(filename))?;
//!   let bytes: Vec<u8> = format.to_utf8(&parsed_document)?;
//!
//!   assert_eq!(std::str::from_utf8(&bytes)?, r##"{
//!   name: {
//!     first: "John",
//!     middle: "Jacob",
//!     last: "Smith",
//!   },
//!   address: {
//!     city: "Anytown",
//!     country: "USA",
//!     state: "New York",
//!     street: "101 Main Street",
//!
//!     /* Update schema to support multiple addresses:
//!        "work": {
//!            "city": "Anytown",
//!            "country": "USA",
//!            "state": "New York",
//!            "street": "101 Main Street"
//!        }
//!     */
//!   },
//!
//!   // Consider adding a note field to the `other` contact option
//!   contact_options: [
//!     {
//!       work: {
//!         phone: "212-555-1234",
//!         email: "john.j.smith@worksforme.gov",
//!       },
//!       home: {
//!         phone: "212-555-4321",
//!         email: "jj@notreallygmail.com", // This was the original user id.
//!                                         // Now user id's are hash values.
//!       },
//!       other: { email: "volunteering@serviceprojectsrus.org" },
//!     },
//!   ],
//!   children: [
//!     "Balto",
//!     "Biff",
//!     "Buffy",
//!   ],
//! }
//! "##);
//! # Ok::<(),anyhow::Error>(())
//! ```
//!
//! # Formatter Actions
//!
//! When the options above are applied to the input, the formatter will make the following changes:
//!
//!   * The formatted document will be indented by 2 spaces.
//!   * Quotes are removed from all property names (since they are all legal ECMAScript identifiers)
//!   * The top-level properties will be reordered to [`name`, `address`, `contact_options`]. Since
//!     property name `children` was not included in the sort order, it will be placed at the end.
//!   * The `name` properties will be reordered to [`first`, `middle`, `last`].
//!   * The properties of the unnamed object in array `contact_options` will be reordered to
//!     [`work`, `home`, `other`].
//!   * The properties of the `work`, `home`, and `other` objects will be reordered to
//!     [`phone`, `email`].
//!   * The `children` names array of string primitives will be sorted.
//!   * All elements (except the top-level object, represented by the outermost curly braces) will
//!     end with a comma.
//!   * Since the `contact_options` descendant element `other` has only one property, the `other`
//!     object structure will collapse to a single line, with internal trailing comma suppressed.
//!   * The line comment will retain its relative position, above `contact_options`.
//!   * The block comment will retain its relative position, inside and at the end of the `address`
//!     object.
//!   * The end-of-line comment after `home`/`email` will retain its relative location (appended at
//!     the end of the `email` value) and any subsequent line comments with the same vertical
//!     alignment are also retained, and vertically adjusted to be left-aligned with the new
//!     position of the first comment line.
//!
//! # Formatter Behavior Details
//!
//! For reference, the following sections detail how the JSON5 formatter verifies and processes
//! JSON5 content.
//!
//! ## Syntax Validation
//!
//! * Structural syntax is checked, such as validating matching braces, property name-colon-value
//!   syntax, enforced separation of values by commas, properly quoted strings, and both block and
//!   line comment extraction.
//! * Non-string literal value syntax is checked (null, true, false, and the various legal formats
//!   for JSON5 Numbers).
//! * Syntax errors produce error messages with the line and column where the problem
//!   was encountered.
//!
//! ## Property Names
//!
//! * Duplicate property names are retained, but may constitute errors in higher-level JSON5
//!   parsers or schema-specific deserializers.
//! * All JSON5 unquoted property name characters are supported, including '$' and '_'. Digits are
//!   the only valid property name character that cannot be the first character. Property names
//!   can also be represented as quoted strings. All valid JSON5 strings, if quoted, are valid
//!   property names (including multi-line strings and quoted numbers).
//!
//! Example:
//! ```json
//!     $_meta_prop: 'Has "double quotes" and \'single quotes\' and \
//! multiple lines with escaped \\ backslash',
//! ```
//!
//! ## Literal Values
//!
//! * JSON5 supports quoting strings (literal values or quoted property names) by either double (")
//!   or single (') quote. The formatter does not change the quotes. Double-quoting is
//!   conventional, but single quotes may be used when quoting strings containing double-quotes, and
//!   leaving the single quotes as-is is preferred.
//! * JSON5 literal values are retained as-is. Strings retain all spacing characters, including
//!   escaped newlines. All other literals (unquoted tokens without spaces, such as false, null,
//!   0.234, 1337, or l33t) are _not_ interpreted syntactically. Other schema-based tools and JSON5
//!   deserializers may flag these invalid values.
//!
//! ## Optional Sorting
//!
//! * By default, array items and object properties retain their original order. (Some JSON arrays
//!   are order-dependent, and sorting them indiscriminantly might change the meaning of the data.)
//! * The formatter can automatically sort array items and object properties if enabled via
//!   `FormatOptions`:
//!   - To sort all arrays in the document, set
//!     [FormatOptions.sort_array_items](struct.FormatOptions.html#structfield.sort_array_items) to
//!     `true`
//!   - To sort only specific arrays in the target schema, specify the schema location under
//!     [FormatOptions.options_by_path](struct.FormatOptions.html#structfield.options_by_path), and
//!     set its [SortArrayItems](enum.PathOption.html#variant.SortArrayItems) option.
//!   - Properties are sorted based on an explicit user-supplied list of property names in the
//!     preferred order, for objects at a specified path. Specify the object's location in the
//!     target schema using
//!     [FormatOptions.options_by_path](struct.FormatOptions.html#structfield.options_by_path), and
//!     provide a vector of property name strings with the
//!     [PropertyNameOrder](enum.PathOption.html#variant.PropertyNameOrder) option. Properties not
//!     included in this option retain their original order, behind the explicitly ordered
//!     properties, if any.
//! * When sorting array items, the formatter only sorts array item literal values (strings,
//!   numbers, bools, and null). Child arrays or objects are left in their original order, after
//!   sorted literals, if any, within the same array.
//! * Array items are sorted in case-insensitive unicode lexicographic order. **(Note that, since
//!   the formatter does not parse unquoted literals, number types cannot be sorted numerically.)**
//!   Items that are case-insensitively equal are re-compared and ordered case-sensitively with
//!   respect to each other.
//!
//! ## Associated Comments
//!
//! * All comments immediately preceding an element (value or start of an array or object), and
//!   trailing line comments (starting on the same line as the element, optionally continued on
//!   successive lines if all line comments are left-aligned), are retained and move with the
//!   associated item if the item is repositioned during sorting.
//! * All line and block comments are retained. Typically, the comments are re-aligned vertically
//!   (indented) with the values with which they were associated.
//! * A single line comment appearing immediately after a JSON value (primitive or closing brace),
//!   on the same line, will remain appended to that value on its line after re-formatting.
//! * Spaces separate block comments from blocks of contiguous line comments associated with the
//!   same entry.
//! * Comments at the end of a list (after the last property or item) are retained at the end of
//!   the same list.
//! * Block comments with lines that extend to the left of the opening "/\*" are not re-aligned.
//!
//! ## Whitespace Handling
//!
//! * Unicode characters are allowed, and unicode space characters should retain their meaning
//!   according to unicode standards.
//! * All spaces inside single- or multi-line strings are retained. All spaces in comments are
//!   retained *except* trailing spaces at the end of a line.
//! * All other original spaces are removed.

#![deny(missing_docs)]

#[macro_use]
mod error;

mod content;
mod formatter;
mod options;
mod parser;

use {
    crate::formatter::*, std::cell::RefCell, std::collections::HashMap, std::collections::HashSet,
    std::rc::Rc,
};

pub use content::Array;
pub use content::Comment;
pub use content::Comments;
pub use content::Object;
pub use content::ParsedDocument;
pub use content::Primitive;
pub use content::Property;
pub use content::Value;
pub use error::Error;
pub use error::Location;
pub use options::FormatOptions;
pub use options::PathOption;

/// Format a JSON5 document, applying a consistent style, with given options.
///
/// See [FormatOptions](struct.FormatOptions.html) for style options, and confirm the defaults by
/// reviewing the source of truth via the `src` link for
/// [impl Default for FormatOptions](struct.FormatOptions.html#impl-Default).
///
/// # Format and Style (Default)
///
/// Unless FormatOptions are modified, the JSON5 formatter takes a JSON5 document (as a unicode
/// String) and generates a new document with the following formatting:
///
/// * Indents 4 spaces.
/// * Quotes are removed from property names if they are legal ECMAScript 5.1 identifiers. Property
///   names that do not comply with ECMAScript identifier format requirements will retain their
///   existing (single or double) quotes.
/// * All property and item lists end with a trailing comma.
/// * All property and item lists are broken down; that is, the braces are on separate lines and
///   all values are indented.
///
/// ```json
/// {
///     key: "value",
///     array: [
///         3.145,
///     ]
/// }
/// ```
///
/// # Arguments
///   * buffer - A unicode string containing the original JSON5 document.
///   * filename - An optional filename. Parsing errors typically include the filename (if given),
///     and the line number and character column where the error was detected.
///   * options - Format style options to override the default style, if provided.
/// # Returns
///   * The formatted result in UTF-8 encoded bytes.
pub fn format(
    buffer: &str,
    filename: Option<String>,
    options: Option<FormatOptions>,
) -> Result<Vec<u8>, Error> {
    let parsed_document = ParsedDocument::from_str(buffer, filename)?;
    let options = match options {
        Some(options) => options,
        None => FormatOptions { ..Default::default() },
    };
    Json5Format::with_options(options)?.to_utf8(&parsed_document)
}

/// A JSON5 formatter that parses a valid JSON5 input buffer and produces a new, formatted document.
pub struct Json5Format {
    /// Options that alter how the formatter generates the formatted output. This instance of
    /// FormatOptions is a subset of the FormatOptions passed to the `with_options` constructor.
    /// The `options_by_path` are first removed, and then used to initialize the SubpathOptions
    /// hierarchy rooted at the `document_root_options_ref`.
    default_options: FormatOptions,

    /// Depth-specific options applied at the document root and below.
    document_root_options_ref: Rc<RefCell<SubpathOptions>>,
}

impl Json5Format {
    /// Create and return a Json5Format, with the given options to be applied to the
    /// [Json5Format::to_utf8()](struct.Json5Format.html#method.to_utf8) operation.
    pub fn with_options(mut options: FormatOptions) -> Result<Self, Error> {
        let mut document_root_options = SubpathOptions::new(&options);

        // Typical JSON5 documents start and end with curly braces for a top-level unnamed
        // object. This is by convention, and the Json5Format represents this
        // top-level object as a single child in a conceptual array. The array square braces
        // are not rendered, and by convention, the child object should not have a trailing
        // comma, even if trailing commas are the default everywhere else in the document.
        //
        // Set the SubpathOptions for the document array items to prevent trailing commas.
        document_root_options.options.trailing_commas = false;

        let mut options_by_path =
            options.options_by_path.drain().collect::<HashMap<&'static str, HashSet<PathOption>>>();

        // Default options remain after draining the `options_by_path`
        let default_options = options;

        // Transfer the options_by_path from the given options into the SubpathOptions tree
        // rooted at `document_options_root`.
        for (path, path_options) in options_by_path.drain() {
            let rc; // extend life of temporary
            let mut borrowed; // extend life of temporary
            let subpath_options = if path == "/" {
                &mut document_root_options
            } else if path.starts_with("/") {
                rc = document_root_options.get_or_create_subpath_options(
                    &path[1..].split('/').collect::<Vec<_>>(),
                    &default_options,
                );
                borrowed = rc.borrow_mut();
                &mut *borrowed
            } else {
                return Err(Error::configuration(format!(
                    "PathOption path '{}' is invalid.",
                    path
                )));
            };
            subpath_options.override_default_options(&path_options);
        }

        Ok(Json5Format {
            default_options,
            document_root_options_ref: Rc::new(RefCell::new(document_root_options)),
        })
    }

    /// Create and return a Json5Format, with the default settings.
    pub fn new() -> Result<Self, Error> {
        Self::with_options(FormatOptions { ..Default::default() })
    }

    /// Formats the parsed document into a new Vector of UTF8 bytes.
    ///
    /// # Arguments
    ///   * `parsed_document` - The parsed state of the incoming document.
    ///
    /// # Example
    ///
    /// ```
    /// # use json5format::*;
    /// # let buffer = String::from("{}");
    /// # let filename = String::from("example.json5");
    /// let format = Json5Format::new()?;
    /// let parsed_document = ParsedDocument::from_str(&buffer, Some(filename))?;
    /// let bytes = format.to_utf8(&parsed_document)?;
    /// # assert_eq!("{}\n", std::str::from_utf8(&bytes).unwrap());
    /// # Ok::<(),anyhow::Error>(())
    /// ```
    pub fn to_utf8(&self, parsed_document: &ParsedDocument) -> Result<Vec<u8>, Error> {
        let formatter =
            Formatter::new(self.default_options.clone(), self.document_root_options_ref.clone());
        formatter.format(parsed_document)
    }

    /// Formats the parsed document into a new String.
    ///
    /// # Arguments
    ///   * `parsed_document` - The parsed state of the incoming document.
    ///
    /// # Example
    ///
    /// ```
    /// # use json5format::*;
    /// # fn main() -> std::result::Result<(), Error> {
    /// # let buffer = String::from("{}");
    /// # let filename = String::from("example.json5");
    /// let format = Json5Format::new()?;
    /// let parsed_document = ParsedDocument::from_str(&buffer, Some(filename))?;
    /// let formatted = format.to_string(&parsed_document)?;
    /// # assert_eq!("{}\n", formatted);
    /// # Ok(())
    /// # }
    /// ```
    pub fn to_string(&self, parsed_document: &ParsedDocument) -> Result<String, Error> {
        String::from_utf8(self.to_utf8(parsed_document)?)
            .map_err(|e| Error::internal(None, e.to_string()))
    }
}
