// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::parser::{self, ArgKind};

#[derive(Debug)]
pub struct Protocol {
    pub name: String,
    pub copyright: Option<String>,
    pub description: Option<Description>,
    pub interfaces: Vec<Interface>,
}

#[derive(Debug)]
pub struct Description {
    pub summary: String,
    pub description: String,
}

#[derive(Debug)]
pub struct Interface {
    pub name: String,
    pub version: u32,
    pub description: Option<Description>,
    pub requests: Vec<Message>,
    pub events: Vec<Message>,
    pub enums: Vec<Enum>,
}

#[derive(Debug)]
pub struct Message {
    pub name: String,
    pub since: u32,
    pub request_type: Option<String>,
    pub description: Option<Description>,
    pub args: Vec<Arg>,
}

#[derive(Debug)]
pub struct Arg {
    pub name: String,
    pub kind: ArgKind,
    pub summary: Option<String>,
    pub interface: Option<String>,
    pub nullable: bool,
    pub enum_type: Option<String>,
    pub description: Option<Description>,
}

#[derive(Debug)]
pub struct Enum {
    pub name: String,
    pub since: u32,
    pub bitfield: bool,
    pub description: Option<Description>,
    pub entries: Vec<EnumEntry>,
}

#[derive(Debug)]
pub struct EnumEntry {
    pub name: String,
    pub value: i64,
    pub summary: Option<String>,
    pub since: u32,
    pub description: Option<Description>,
}

pub type AstError = String;
pub type AstResult<T> = Result<T, AstError>;

fn build_protocol(node: parser::ParseNode) -> AstResult<Protocol> {
    if let parser::ParseElement::Protocol { name } = node.element {
        let mut copyright: Option<String> = None;
        let mut description: Option<Description> = None;
        let mut interfaces: Vec<Interface> = Vec::new();
        for child in node.children {
            match &child.element {
                parser::ParseElement::Copyright => copyright = Some(build_copyright(child)?),
                parser::ParseElement::Description { .. } => {
                    description = Some(build_description(child)?)
                }
                parser::ParseElement::Interface { .. } => interfaces.push(build_interface(child)?),
                _ => return Err("Unsupported".to_owned()),
            }
        }
        Ok(Protocol { name: name, copyright, description, interfaces })
    } else {
        Err("Unexpected Element; expected Protocol".to_owned())
    }
}

fn build_copyright(node: parser::ParseNode) -> AstResult<String> {
    if let Some(copyright) = node.body {
        Ok(copyright)
    } else {
        Err(format!("Unexpected node {:?}", node))
    }
}

fn build_description(node: parser::ParseNode) -> AstResult<Description> {
    if let parser::ParseElement::Description { summary } = node.element {
        Ok(Description { summary, description: node.body.unwrap_or("".to_owned()) })
    } else {
        Err("Invalid node".to_owned())
    }
}

fn build_interface(node: parser::ParseNode) -> AstResult<Interface> {
    if let parser::ParseElement::Interface { name, version } = node.element {
        let mut description: Option<Description> = None;
        let mut requests: Vec<Message> = Vec::new();
        let mut events: Vec<Message> = Vec::new();
        let mut enums: Vec<Enum> = Vec::new();
        for child in node.children {
            match &child.element {
                parser::ParseElement::Description { .. } => {
                    description = Some(build_description(child)?)
                }
                parser::ParseElement::Request { .. } => requests.push(build_request(child)?),
                parser::ParseElement::Event { .. } => events.push(build_event(child)?),
                parser::ParseElement::Enum { .. } => enums.push(build_enum(child)?),
                _ => return Err("Unsupported".to_owned()),
            }
        }
        Ok(Interface { name, version, description, requests, events, enums })
    } else {
        Err("Invalid node".to_owned())
    }
}

fn build_request(node: parser::ParseNode) -> AstResult<Message> {
    if let parser::ParseElement::Request { name, since, request_type } = node.element {
        let mut description: Option<Description> = None;
        let mut args: Vec<Arg> = Vec::new();
        for child in node.children {
            match &child.element {
                parser::ParseElement::Description { .. } => {
                    description = Some(build_description(child)?)
                }
                parser::ParseElement::Arg { .. } => args.append(&mut build_arg(child)?),
                _ => return Err("Unsupported".to_owned()),
            }
        }
        Ok(Message { name, since, request_type, description, args })
    } else {
        Err("Invalid node".to_owned())
    }
}

fn build_event(node: parser::ParseNode) -> AstResult<Message> {
    if let parser::ParseElement::Event { name, since } = node.element {
        let mut description: Option<Description> = None;
        let mut args: Vec<Arg> = Vec::new();
        for child in node.children {
            match &child.element {
                parser::ParseElement::Description { .. } => {
                    description = Some(build_description(child)?)
                }
                parser::ParseElement::Arg { .. } => args.append(&mut build_arg(child)?),
                _ => return Err("Unsupported".to_owned()),
            }
        }
        Ok(Message { name, since, description, args, request_type: None })
    } else {
        Err("Invalid node".to_owned())
    }
}

fn build_arg(node: parser::ParseNode) -> AstResult<Vec<Arg>> {
    if let parser::ParseElement::Arg { name, kind, summary, interface, nullable, enum_type } =
        node.element
    {
        let mut description: Option<Description> = None;
        for child in node.children {
            match &child.element {
                parser::ParseElement::Description { .. } => {
                    description = Some(build_description(child)?)
                }
                _ => return Err("Unsupported".to_owned()),
            }
        }
        let arg = Arg { name, kind, summary, interface, nullable, enum_type, description };
        // wayland has a slightly different serialization of untyped new_id
        // arguments. Instead of just sending the object id, the interface name
        // and version are sent first. This primarily impacts the
        // wl_registry::bind request.
        if arg.kind == ArgKind::NewId && arg.interface.is_none() {
            Ok(vec![
                Arg {
                    name: format!("{}_interface_name", arg.name),
                    kind: ArgKind::String,
                    summary: None,
                    interface: None,
                    nullable: false,
                    enum_type: None,
                    description: None,
                },
                Arg {
                    name: format!("{}_interface_version", arg.name),
                    kind: ArgKind::Uint,
                    summary: None,
                    interface: None,
                    nullable: false,
                    enum_type: None,
                    description: None,
                },
                arg,
            ])
        } else {
            Ok(vec![arg])
        }
    } else {
        Err("Invalid node".to_owned())
    }
}

fn build_enum(node: parser::ParseNode) -> AstResult<Enum> {
    if let parser::ParseElement::Enum { name, since, bitfield } = node.element {
        let mut description: Option<Description> = None;
        let mut entries: Vec<EnumEntry> = Vec::new();
        for child in node.children {
            match &child.element {
                parser::ParseElement::Description { .. } => {
                    description = Some(build_description(child)?)
                }
                parser::ParseElement::EnumEntry { .. } => entries.push(build_enum_entry(child)?),
                _ => return Err("Unsupported".to_owned()),
            }
        }
        Ok(Enum { name, since, bitfield, description, entries })
    } else {
        Err("Invalid node".to_owned())
    }
}

fn build_enum_entry(node: parser::ParseNode) -> AstResult<EnumEntry> {
    if let parser::ParseElement::EnumEntry { name, value, summary, since } = node.element {
        let mut description: Option<Description> = None;
        for child in node.children {
            match &child.element {
                parser::ParseElement::Description { .. } => {
                    description = Some(build_description(child)?)
                }
                _ => return Err("Unsupported".to_owned()),
            }
        }
        Ok(EnumEntry { name, value, summary, since, description })
    } else {
        Err("Invalid node".to_owned())
    }
}

impl Protocol {
    pub fn from_parse_tree(parse_tree: parser::ParseNode) -> AstResult<Protocol> {
        build_protocol(parse_tree)
    }
}
