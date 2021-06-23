// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use xml::reader::{EventReader, XmlEvent};

use std::error;
use std::fmt;
use std::io::Read;
use std::str::{FromStr, ParseBoolError};

#[derive(Debug, PartialEq)]
pub enum ArgKind {
    Int,
    Uint,
    Fixed,
    Object,
    NewId,
    String,
    Array,
    Fd,
}

impl ArgKind {
    fn from_str(s: &str) -> Option<ArgKind> {
        match s {
            "int" => Some(ArgKind::Int),
            "uint" => Some(ArgKind::Uint),
            "fixed" => Some(ArgKind::Fixed),
            "string" => Some(ArgKind::String),
            "object" => Some(ArgKind::Object),
            "new_id" => Some(ArgKind::NewId),
            "array" => Some(ArgKind::Array),
            "fd" => Some(ArgKind::Fd),
            _ => None,
        }
    }
}

#[derive(Debug)]
pub enum ParseElement {
    Protocol {
        name: String,
    },
    Copyright,
    Interface {
        name: String,
        version: u32,
    },
    Description {
        summary: String,
    },
    Request {
        name: String,
        since: u32,
        request_type: Option<String>,
    },
    Event {
        name: String,
        since: u32,
    },
    Arg {
        name: String,
        kind: ArgKind,
        summary: Option<String>,
        interface: Option<String>,
        nullable: bool,
        enum_type: Option<String>,
    },
    Enum {
        name: String,
        since: u32,
        bitfield: bool,
    },
    EnumEntry {
        name: String,
        value: i64,
        summary: Option<String>,
        since: u32,
    },
}

#[derive(Debug)]
pub struct ParseNode {
    pub element: ParseElement,
    pub body: Option<String>,
    pub children: Vec<ParseNode>,
}

pub struct Parser<R: Read> {
    xml: xml::reader::EventReader<R>,
}

#[derive(Debug, Clone)]
pub struct ParseError {
    msg: String,
}

impl ParseError {
    fn new(msg: String) -> Self {
        ParseError { msg }
    }
}

impl fmt::Display for ParseError {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "{}", self.msg)
    }
}

impl error::Error for ParseError {
    fn description(&self) -> &str {
        &self.msg
    }

    fn cause(&self) -> Option<&dyn error::Error> {
        None
    }
}

impl From<ParseBoolError> for ParseError {
    fn from(e: ParseBoolError) -> Self {
        ParseError::new(format!("Failed to parse boolean {}", e))
    }
}

pub type ParseResult<T> = Result<T, ParseError>;

/// The default rust numeric parse routines do not handle '0x..' constants.
/// This just maps 0x to a radix of 16.
fn parse_int<T: FromStr + num::Num>(s: &str) -> ParseResult<T> {
    if s.starts_with("0x") {
        T::from_str_radix(&s[2..], 16)
            .map_err(|_| ParseError::new(format!("Failed to parse int {}", s)))
    } else {
        s.parse::<T>().map_err(|_| ParseError::new(format!("Failed to parse int {}", s)))
    }
}

trait XmlAttr
where
    Self: Sized,
{
    fn default() -> Self;

    fn from_xml_attr(attr: xml::attribute::OwnedAttribute) -> ParseResult<Self>;
}

impl<T> XmlAttr for Option<T>
where
    T: XmlAttr,
{
    fn default() -> Self {
        None
    }

    fn from_xml_attr(attr: xml::attribute::OwnedAttribute) -> ParseResult<Self> {
        T::from_xml_attr(attr).map(|t| Some(t))
    }
}

macro_rules! impl_xml_attr {
    ($type:ty, $default:expr, $attr:ident => $from_attr:expr) => {
        impl XmlAttr for $type {
            fn default() -> Self {
                $default
            }

            fn from_xml_attr($attr: xml::attribute::OwnedAttribute) -> ParseResult<Self> {
                $from_attr
            }
        }
    };
}

impl_xml_attr!(u32, 0, attr => parse_int::<Self>(&attr.value));
impl_xml_attr!(i64, 0, attr => parse_int::<Self>(&attr.value));
impl_xml_attr!(bool, false, attr => Ok(attr.value.parse::<bool>()?));
impl_xml_attr!(String, "".to_owned(), attr => Ok(attr.value));
impl_xml_attr!(Option<ArgKind>, None, attr => Ok(ArgKind::from_str(&attr.value)));

/// Simplifies binding a set of XML attributes to an expression.
///
///
/// Ex:
///
///   // Reads the XML attribute with the name 'attr1' and stores the value into
///   // |var1|. If |attrs| does not contain 'attr1' then |var1| will be None.
///   map_xml_attrs!(attrs, ("attr1" => var1: Option<String>) => {
///         println!("Found attr1 == {}", var1)
///   });
///
/// There must be a corresponding |XmlAttr| trait implementation for any type
/// that appears in the argument list.
macro_rules! map_xml_attrs {
    ($attributes:expr, ($($xml_name:expr => $field_name:ident : $field_type:ty),*) => { $body:expr }) => {
        {
            $(
                let mut $field_name : $field_type = XmlAttr::default();
            )*
            for attr in $attributes {
                match attr.name.local_name.as_ref() {
                    $(
                        $xml_name => $field_name = XmlAttr::from_xml_attr(attr)?,
                    )*
                    _ => {
                        return Err(ParseError::new(format!(
                            "Unsupported attribute {}",
                            attr.name.local_name
                        )));
                    }
                }
            }
            $body
        }
    }
}

impl<R: Read> Parser<R> {
    /// Reads the entire XML document, returning the root node of the parse
    /// tree.
    pub fn read_document(&mut self) -> ParseResult<ParseNode> {
        if let Ok(XmlEvent::StartDocument { .. }) = self.xml.next() {
            self.read_node()
        } else {
            Err(ParseError::new("Missing StartDocument".to_owned()))
        }
    }

    fn read_node(&mut self) -> ParseResult<ParseNode> {
        match self.xml.next() {
            Ok(XmlEvent::StartElement { name, attributes, .. }) => {
                let element = self.element_from_xml(name.local_name.as_ref(), attributes)?;
                self.populate_node(element, name.local_name.as_ref())
            }
            node => Err(ParseError::new(format!("Not Implemented {:?}", node))),
        }
    }

    fn populate_node(&mut self, element: ParseElement, xml_name: &str) -> ParseResult<ParseNode> {
        let mut children: Vec<ParseNode> = Vec::new();
        let mut body: Option<String> = None;
        loop {
            match self.xml.next() {
                Ok(XmlEvent::EndElement { name }) => {
                    if name.local_name == xml_name {
                        return Ok(ParseNode { element, children, body });
                    }
                    return Err(ParseError::new("Unexpected EndElement".to_owned()));
                }
                Ok(XmlEvent::StartElement { name, attributes, .. }) => {
                    let element = self.element_from_xml(name.local_name.as_ref(), attributes)?;
                    let node = self.populate_node(element, name.local_name.as_ref())?;
                    children.push(node);
                }
                Ok(XmlEvent::CData(text)) | Ok(XmlEvent::Characters(text)) => {
                    body = Some(text);
                }
                Ok(XmlEvent::Whitespace(_)) | Ok(XmlEvent::Comment(_)) => continue,
                event => {
                    return Err(ParseError::new(format!("Unhandled event {:?}", event)));
                }
            }
        }
    }

    fn element_from_xml(
        &self,
        element_name: &str,
        attributes: Vec<xml::attribute::OwnedAttribute>,
    ) -> ParseResult<ParseElement> {
        match element_name {
            "protocol" => map_xml_attrs!(attributes,
                                      ("name" => name: Option<String>) => {
                Ok(ParseElement::Protocol {
                    name: name
                        .ok_or_else(|| ParseError::new("Missing 'name' attribute".to_owned()))?,
                })
            }),
            "copyright" => Ok(ParseElement::Copyright),
            "interface" => map_xml_attrs!(attributes,
                                           ("name" => name: Option<String>,
                                            "version" => version: u32) => {
                Ok(ParseElement::Interface {
                    name: name
                        .ok_or_else(|| ParseError::new("Missing 'name' attribute".to_owned()))?,
                    version,
                })

            }),
            "request" => map_xml_attrs!(attributes,
                                         ("name" => name: Option<String>,
                                          "type" => request_type: Option<String>,
                                          "since" => since: u32) => {

                Ok(ParseElement::Request {
                    name: name
                        .ok_or_else(|| ParseError::new("Missing 'name' attribute".to_owned()))?,
                    request_type,
                    since,
                })
            }),
            "event" => map_xml_attrs!(attributes,
                                       ("name" => name: Option<String>,
                                        "since" => since: u32) => {
                Ok(ParseElement::Event {
                    name: name
                        .ok_or_else(|| ParseError::new("Missing 'name' attribute".to_owned()))?,
                    since,
                })
            }),
            "enum" => map_xml_attrs!(attributes,
                                      ("name" => name: Option<String>,
                                       "since" => since: u32,
                                       "bitfield" => bitfield: bool) => {
                Ok(ParseElement::Enum {
                    name: name
                        .ok_or_else(|| ParseError::new("Missing 'name' attribute".to_owned()))?,
                    since,
                    bitfield,
                })
            }),
            "entry" => map_xml_attrs!(attributes,
                                       ("name" => name: Option<String>,
                                        "value" => value: Option<i64>,
                                        "since" => since: u32,
                                        "summary" => summary: Option<String>) => {
                Ok(ParseElement::EnumEntry {
                    name: name
                        .ok_or_else(|| ParseError::new("Missing 'name' attribute".to_owned()))?,
                    value: value
                        .ok_or_else(|| ParseError::new("Missing 'value' attribute".to_owned()))?,
                    since,
                    summary,
                })
            }),
            "arg" => map_xml_attrs!(attributes,
                                     ("name" => name: Option<String>,
                                      "type" => kind: Option<ArgKind>,
                                      "summary" => summary: Option<String>,
                                      "interface" => interface: Option<String>,
                                      "allow-null" => nullable: bool,
                                      "enum" => enum_type: Option<String>) => {

                Ok(ParseElement::Arg {
                    name: name.ok_or_else(|| {
                        ParseError::new("Missing 'name' attributue on argument".to_owned())
                    })?,
                    kind: kind.ok_or_else(|| {
                        ParseError::new("Missing 'type' attributue on argument".to_owned())
                    })?,
                    summary,
                    interface,
                    nullable,
                    enum_type,
                })
            }),
            "description" => map_xml_attrs!(attributes,
                                             ("summary" => summary: Option<String>) => {
                Ok(ParseElement::Description {
                    summary: summary.ok_or_else(|| {
                        ParseError::new("Missing 'summary' attributue on argument".to_owned())
                    })?,
                })
            }),
            tag => Err(ParseError::new(format!("Tag not implemented {}", tag))),
        }
    }
}

impl<R: Read> Parser<R> {
    pub fn new(source: R) -> Parser<R> {
        Parser { xml: EventReader::new(source) }
    }
}

impl<'a> Parser<&'a [u8]> {
    pub fn from_str(protocol: &str) -> Parser<&[u8]> {
        Parser { xml: EventReader::from_str(protocol) }
    }
}
