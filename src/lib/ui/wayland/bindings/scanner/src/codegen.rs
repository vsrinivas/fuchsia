// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::borrow::Cow;
use std::fmt::{Display, Formatter, Result as FmtResult};
use std::io;
use std::str::FromStr;

use crate::ast;
use crate::parser::ArgKind;

pub type IoResult = io::Result<()>;

#[derive(Debug)]
pub enum CodegenTarget {
    Client,
    Server,
}

#[derive(Debug)]
pub enum ParseCodegenTargetError {
    TargetNotFound,
}

impl Display for ParseCodegenTargetError {
    fn fmt(&self, f: &mut Formatter<'_>) -> FmtResult {
        write!(f, "{}", "TargetNotFound")
    }
}

impl FromStr for CodegenTarget {
    type Err = ParseCodegenTargetError;

    fn from_str(s: &str) -> Result<Self, Self::Err> {
        match s {
            "client" => Ok(CodegenTarget::Client),
            "server" => Ok(CodegenTarget::Server),
            _ => Err(ParseCodegenTargetError::TargetNotFound),
        }
    }
}

pub struct Codegen<W: io::Write> {
    w: W,
}

impl<W: io::Write> Codegen<W> {
    pub fn new(w: W) -> Codegen<W> {
        Codegen { w }
    }

    pub fn codegen(
        &mut self,
        target: CodegenTarget,
        protocol: ast::Protocol,
        dependencies: &[String],
    ) -> IoResult {
        self.codegen_protocol(target, protocol, dependencies)
    }

    fn codegen_protocol(
        &mut self,
        target: CodegenTarget,
        protocol: ast::Protocol,
        dependencies: &[String],
    ) -> IoResult {
        writeln!(self.w, "// GENERATED FILE -- DO NOT EDIT")?;
        if let Some(ref c) = protocol.copyright {
            writeln!(self.w, "//")?;
            for line in c.trim().lines() {
                writeln!(self.w, "// {}", line.trim())?;
            }
        }
        writeln!(
            self.w,
            "
#![allow(warnings)]
use bitflags::*;
use anyhow;
use fuchsia_trace;
use fuchsia_wayland_core::{{ArgKind, Arg, Array, Enum, Fixed, FromArgs, IntoMessage, Message,
                            MessageGroupSpec, MessageHeader, MessageSpec, MessageType,
                            NewId, NewObject, ObjectId, EncodeError, DecodeError,
                            Interface }};",
        )?;
        for dep in dependencies.iter() {
            writeln!(self.w, "use {}::*;", dep)?;
        }

        for interface in protocol.interfaces.into_iter() {
            // Most symbols will be defined in a nested module, but re-export
            // some into the top-level namespace.
            //
            // Ex, for wl_display:
            //
            // pub mod wl_display {
            //      pub enum Request { ... }
            //      pub enum Event { ... }
            //      pub struct WlDisplay;
            // }
            //
            // pub use wl_display::WlDisplay;
            // pub use wl_display::Request as WlDisplayRequest;
            // pub use wl_display::Event as WlDisplayEvent;
            writeln!(self.w, "pub mod {} {{", interface.name)?;
            writeln!(self.w, "use super::*;")?;
            self.codegen_interface_trait(&target, &interface)?;
            self.codegen_message_enums(&target, &interface)?;
            self.codegen_message_traits(&target, &interface)?;
            self.codegen_enum_types(&interface)?;
            writeln!(self.w, "}} // mod {}", interface.name)?;
            writeln!(self.w, "")?;
            writeln!(self.w, "pub use crate::{}::{};", interface.name, interface.rust_name())?;
            writeln!(
                self.w,
                "pub use crate::{}::Request as {}Request;",
                interface.name,
                interface.rust_name()
            )?;
            writeln!(
                self.w,
                "pub use crate::{}::Event as {}Event;",
                interface.name,
                interface.rust_name()
            )?;
        }
        Ok(())
    }

    /// Emits a pair of enums that describes the set of messages for a single
    /// interface.  Each interface will have a message enum for both Requests
    /// and Events.
    ///
    /// Ex:
    ///  pub enum MyInterfaceRequest {
    ///    Request1 { arg1: u32 },
    ///    Request2 { name: String},
    ///  }
    ///  pub enum MyInterfaceEvent {
    ///    Event1 { value: Fixed },
    ///    Event2 { new_obj: NewId},
    ///  }
    fn codegen_message_enums(
        &mut self,
        target: &CodegenTarget,
        interface: &ast::Interface,
    ) -> IoResult {
        let enum_descriptions: [(&str, &Vec<ast::Message>, ArgFormatterFn); 2] = match target {
            CodegenTarget::Client => [
                ("Request", &interface.requests, format_wire_arg_rust),
                ("Event", &interface.events, format_dispatch_arg_rust),
            ],
            CodegenTarget::Server => [
                ("Request", &interface.requests, format_dispatch_arg_rust),
                ("Event", &interface.events, format_wire_arg_rust),
            ],
        };
        for (name, messages, arg_formatter) in enum_descriptions {
            writeln!(self.w, "#[derive(Debug)]")?;
            writeln!(self.w, "pub enum {name} {{")?;
            for message in messages.iter() {
                if let Some(ref d) = message.description {
                    self.codegen_description(d, "    ")?;
                }
                if message.args.is_empty() {
                    // For messages without args, emit a marker enum variant.
                    //  Ex:
                    //      Request::Message,
                    writeln!(self.w, "    {},", message.rust_name())?;
                } else {
                    // For messages with args, emit a struct enum variant with an
                    // entry for each arg:
                    //  Ex:
                    //      Request::Message {
                    //          arg1: u32,
                    //          arg2: String,
                    //      },
                    writeln!(self.w, "    {} {{", message.rust_name())?;
                    for arg in message.args.iter() {
                        if let Some(ref summary) = arg.summary {
                            for line in summary.lines() {
                                writeln!(self.w, "        /// {}", line.trim())?;
                            }
                        }
                        writeln!(
                            self.w,
                            "        {arg_name}: {arg_type},",
                            arg_name = arg.rust_name(),
                            arg_type = arg_formatter(&arg)
                        )?;
                    }
                    writeln!(self.w, "    }},")?;
                }
            }
            writeln!(self.w, "}}")?;
            writeln!(self.w, "")?;

            writeln!(self.w, "impl MessageType for {} {{", name)?;

            // Generate the log method:
            //
            // fn log(&self, this: ObjectId) -> String {
            //     let mut string = String::new();
            //     match *self {
            //         WlInterface::Message { ref arg } =>
            //             format!("wl_interface@{}::message(arg: {:?})", this, arg),
            //         ...
            //     }
            // }
            writeln!(self.w, "    fn log(&self, this: ObjectId) -> String {{")?;
            writeln!(self.w, "        match *self {{")?;
            for message in messages.iter() {
                writeln!(self.w, "            {}::{} {{", name, message.rust_name())?;
                for arg in message.args.iter() {
                    writeln!(self.w, "                ref {},", arg.rust_name())?;
                }
                writeln!(self.w, "            }} => {{")?;

                // We're using format strings to build a format string, so this is
                // a little confusing. |message_args| are the set of strings that
                // will be joined to form the format string literal. |format_args|
                // are the rust expressions that will be used by the format string.
                //
                // Anytime we put a '{{}}' into |message_args|, we'll need a
                // corresponding expression pushed to |format_args|.
                //
                // We'll end up with something like:
                //      format!("some_interface@3::message1(arg1: {}, arg2: {})", arg1, arg2)
                write!(
                    self.w,
                    "                format!(\"{}@{{:?}}::{}(",
                    interface.name, message.name
                )?;
                let mut message_args = vec![];
                let mut format_args: Vec<Cow<'_, str>> = vec!["this".into()];
                for arg in message.args.iter() {
                    match arg.kind {
                        ArgKind::Array => {
                            message_args.push(format!("{}: Array[{{}}]", arg.name));
                            format_args.push(format!("{}.len()", arg.rust_name()).into());
                        }
                        ArgKind::Fd => {
                            message_args.push(format!("{}: <handle>", arg.name));
                        }
                        _ => {
                            message_args.push(format!("{}: {{:?}}", arg.name));
                            format_args.push(arg.rust_name().into());
                        }
                    }
                }
                writeln!(self.w, "{})\", {})", message_args.join(", "), format_args.join(", "))?;
                writeln!(self.w, "            }}")?;
            }
            writeln!(self.w, "        }}")?;
            writeln!(self.w, "    }}")?;

            writeln!(self.w, "    fn message_name(&self) -> &'static std::ffi::CStr{{")?;
            writeln!(self.w, "        match *self {{")?;
            for message in messages.iter() {
                writeln!(
                    self.w,
                    "            {}::{} {{ .. }} => fuchsia_trace::cstr!(\"{}::{}\"),",
                    name,
                    message.rust_name(),
                    interface.name,
                    message.name
                )?;
            }
            writeln!(self.w, "        }}")?;
            writeln!(self.w, "    }}")?;

            writeln!(self.w, "}}")?;
        }
        Ok(())
    }

    fn codegen_message_traits(
        &mut self,
        target: &CodegenTarget,
        interface: &ast::Interface,
    ) -> IoResult {
        let (into_message_name, from_args_name, into_message_messages, from_args_messages) =
            match target {
                CodegenTarget::Client => {
                    ("Request", "Event", &interface.requests, &interface.events)
                }
                CodegenTarget::Server => {
                    ("Event", "Request", &interface.events, &interface.requests)
                }
            };

        self.codegen_into_message(into_message_name, into_message_messages)?;
        self.codegen_from_args(from_args_name, from_args_messages)?;
        Ok(())
    }

    /// Generates impls for the IntoMessage trait for a type of messages.
    /// For clients, the type used is Request.  For servers, it is Event.
    /// This will be the code that allows the message type to be serialized into
    /// a Message that can be sent over channel.
    ///
    /// Ex:
    ///   impl IntoMessage for Request {
    ///       fn into_message(self, id: u32) -> Result<Message, <Self as IntoMessage>::Error> {
    ///           let mut header = MessageHeader {...};
    ///           let mut message = Message::new();
    ///           message.write_header(&header);
    ///           match self {
    ///           MyInterfaceRequest::Request1 { uint_arg } => {
    ///               message.write_arg(Arg::Uint(uint_arg))?;
    ///               header.opcode = 0;
    ///           },
    ///           // ... Encode other requests...
    ///           }
    ///
    ///           // Rewrite header with proper ordinal & length.
    ///           header.length = msg.bytes().len() as u16;
    ///           message.rewind();
    ///           message.write_header(&header);
    ///       }
    ///   }
    fn codegen_into_message(&mut self, name: &str, messages: &Vec<ast::Message>) -> IoResult {
        write!(
            self.w,
            "\
impl IntoMessage for {name} {{
    type Error = EncodeError;
    fn into_message(self, id: u32) -> Result<Message, <Self as IntoMessage>::Error> {{
        let mut header = MessageHeader {{
            sender: id,
            opcode: 0,
            length: 0,
        }};
        let mut msg = Message::new();
        msg.write_header(&header)?;
        match self {{"
        )?;

        for (op, message) in messages.iter().enumerate() {
            write!(
                self.w,
                "
        {name}::{message_name} {{\n",
                message_name = to_camel_case(&message.name)
            )?;
            for arg in message.args.iter() {
                write!(self.w, "            {arg_name},\n", arg_name = arg.rust_name())?;
            }
            write!(self.w, "        }} => {{\n")?;
            for arg in message.args.iter() {
                write!(
                    self.w,
                    "            msg.write_arg({arg})?;\n",
                    arg = format_wire_arg(&arg, &arg.rust_name())
                )?;
            }
            write!(
                self.w,
                "            header.opcode = {opcode};
        }},",
                opcode = op
            )?;
        }
        write!(
            self.w,
            "
        }}
        header.length = msg.bytes().len() as u16;
        msg.rewind();
        msg.write_header(&header)?;
        Ok(msg)
    }}
}}\n"
        )
    }

    /// Generates impls for the FromArgs trait for a type of messages.
    /// For clients, the type used is Event.  For servers, it is Request.
    /// This will be the code that allows the message type to be deserialized
    /// from a Message received from a channel.
    ///
    /// Ex:
    ///   impl FromArgs for Request {
    ///       fn from_args(op: u16, mut args: Vec<Arg>) -> Result<Self, anyhow::Error> {
    ///           match op {
    ///           0 /* Sync */ => {
    ///             // ... Parse Sync Event...
    ///           }
    ///           // ... Encode other events...
    ///           }
    ///       }
    ///   }
    fn codegen_from_args(&mut self, name: &str, messages: &Vec<ast::Message>) -> IoResult {
        write!(
            self.w,
            "\
impl FromArgs for {name} {{
    fn from_args(op: u16, mut args: Vec<Arg>) -> Result<Self, anyhow::Error> {{
        match op {{",
        )?;

        for (op, message) in messages.iter().enumerate() {
            write!(
                self.w,
                "
        {opcode} /* {op_name} */ => {{
            let mut iter = args.into_iter();
            Ok({name}::{message_name} {{\n",
                opcode = op,
                op_name = message.name,
                message_name = to_camel_case(&message.name)
            )?;
            for arg in message.args.iter() {
                writeln!(
                    self.w,
                    "                {}: iter.next()
                                             .ok_or(DecodeError::InsufficientArgs)?
                                             .{},",
                    arg.rust_name(),
                    arg_to_primitive(&arg)
                )?;
            }
            write!(
                self.w,
                "
            }})
        }},"
            )?;
        }
        write!(
            self.w,
            "
        _ => {{
            Err(DecodeError::InvalidOpcode(op).into())
        }},
        }}
    }}
}}\n"
        )
    }

    /// Generates a trait for each interface.
    ///
    /// Ex:
    ///   pub struct MyInterface;
    ///
    ///   impl Interface for MyInterface {
    ///       const NAME: &'static str = "my_interface";
    ///       const VERSION: u32 = 0;
    ///       type Request = MyInterfaceRequest;
    ///       type Event = MyInterfaceEvent;
    ///   }
    fn codegen_interface_trait(
        &mut self,
        target: &CodegenTarget,
        interface: &ast::Interface,
    ) -> IoResult {
        let interface_name = to_camel_case(&interface.name);
        if let Some(ref d) = interface.description {
            self.codegen_description(d, "")?;
        }
        let (incoming_type, outgoing_type) = match target {
            CodegenTarget::Client => ("Event", "Request"),
            CodegenTarget::Server => ("Request", "Event"),
        };
        writeln!(self.w, "#[derive(Debug)]")?;
        writeln!(self.w, "pub struct {};", interface_name)?;
        writeln!(self.w, "")?;
        writeln!(self.w, "impl Interface for {} {{", interface_name)?;
        writeln!(self.w, "    const NAME: &'static str = \"{}\";", interface.name)?;
        writeln!(self.w, "    const VERSION: u32 = {};", interface.version)?;
        write!(self.w, "    const REQUESTS: MessageGroupSpec = ")?;
        self.codegen_message_group_spec(&interface.requests)?;
        write!(self.w, "    const EVENTS: MessageGroupSpec = ")?;
        self.codegen_message_group_spec(&interface.events)?;
        writeln!(self.w, "    type Incoming = {incoming_type};")?;
        writeln!(self.w, "    type Outgoing = {outgoing_type};")?;
        writeln!(self.w, "}}")?;
        writeln!(self.w, "")?;
        Ok(())
    }

    fn codegen_message_group_spec(&mut self, messages: &Vec<ast::Message>) -> IoResult {
        writeln!(self.w, "MessageGroupSpec(&[")?;
        for m in messages.iter() {
            writeln!(self.w, "        // {}", m.name)?;
            writeln!(self.w, "        MessageSpec(&[")?;
            for arg in m.args.iter() {
                writeln!(self.w, "            {},", format_arg_kind(&arg))?;
            }
            writeln!(self.w, "        ]),")?;
        }
        writeln!(self.w, "    ]);")?;
        Ok(())
    }

    fn codegen_enum_types(&mut self, interface: &ast::Interface) -> IoResult {
        for e in interface.enums.iter() {
            if e.bitfield {
                self.codegen_bitflags_enum(e)?;
            } else {
                self.codegen_value_enum(e)?;
            }
            self.codegen_enum_into_arg(e)?;
        }
        Ok(())
    }

    fn codegen_enum_into_arg(&mut self, e: &ast::Enum) -> IoResult {
        writeln!(self.w, "impl Into<Arg> for {} {{", e.rust_name())?;
        writeln!(self.w, "    fn into(self) -> Arg {{")?;
        writeln!(self.w, "        Arg::Uint(self.bits())")?;
        writeln!(self.w, "    }}")?;
        writeln!(self.w, "}}")?;
        Ok(())
    }

    fn codegen_value_enum(&mut self, e: &ast::Enum) -> IoResult {
        if let Some(ref d) = e.description {
            self.codegen_description(d, "")?;
        }
        writeln!(self.w, "#[derive(Copy, Clone, Debug, Eq, PartialEq)]")?;
        writeln!(self.w, "#[repr(u32)]")?;
        writeln!(self.w, "pub enum {} {{", e.rust_name())?;
        for entry in e.entries.iter() {
            if let Some(ref s) = entry.summary {
                for l in s.lines() {
                    writeln!(self.w, "    /// {},", l.trim())?;
                }
            }
            writeln!(self.w, "    {} = {},", entry.rust_name(), entry.value)?;
        }
        writeln!(self.w, "}}")?;
        writeln!(self.w, "")?;
        writeln!(self.w, "impl {} {{", e.rust_name())?;
        writeln!(self.w, "    pub fn from_bits(v: u32) -> Option<Self> {{")?;
        writeln!(self.w, "        match v {{")?;
        for entry in e.entries.iter() {
            writeln!(
                self.w,
                "        {} => Some({}::{}),",
                entry.value,
                e.rust_name(),
                entry.rust_name()
            )?;
        }
        writeln!(self.w, "        _ => None,")?;
        writeln!(self.w, "        }}")?;
        writeln!(self.w, "    }}")?;
        writeln!(self.w, "")?;
        writeln!(self.w, "    pub fn bits(&self) -> u32 {{")?;
        writeln!(self.w, "        *self as u32")?;
        writeln!(self.w, "    }}")?;
        writeln!(self.w, "}}")?;
        Ok(())
    }

    fn codegen_bitflags_enum(&mut self, e: &ast::Enum) -> IoResult {
        writeln!(self.w, "::bitflags::bitflags! {{")?;
        if let Some(ref d) = e.description {
            self.codegen_description(d, "    ")?;
        }
        writeln!(self.w, "    pub struct {}: u32 {{", e.rust_name())?;
        for entry in e.entries.iter() {
            if let Some(ref s) = entry.summary {
                for l in s.lines() {
                    writeln!(self.w, "        /// {},", l.trim())?;
                }
            }
            writeln!(self.w, "        const {} = {};", entry.rust_name(), entry.value)?;
        }
        writeln!(self.w, "    }}")?;
        writeln!(self.w, "}}")?;
        Ok(())
    }

    fn codegen_description(&mut self, d: &ast::Description, prefix: &str) -> IoResult {
        writeln!(self.w, "")?;
        for s in d.summary.as_str().trim().lines() {
            writeln!(self.w, "{}/// {}", prefix, s.trim())?;
        }
        writeln!(self.w, "{}///", prefix)?;
        for s in d.description.trim().lines() {
            writeln!(self.w, "{}/// {}", prefix, s.trim())?;
        }
        Ok(())
    }
}

fn to_camel_case(s: &str) -> String {
    s.split('_').filter(|s| s.len() > 0).map(|s| s[..1].to_uppercase() + &s[1..]).collect()
}

/// Enums can be referenced outside of the interface that defines them. When
/// arguments are tagged with an enum, they'll provide the path to the enum
/// in the form <interface>.<enum>.
///
/// For example, 'wl_output.transform' refers to the enum named 'transform'
/// that's defined in the 'wl_output' interface.
///
/// Since our rust modules already mirror this structure, we can simply use a
/// relative path to the enum when it's defined in the same interface, or
/// reference the interface module for foreign enums.
///
/// Ex:
///   Within the 'wl_output' module, the 'transform' enum can be referred to
///   as just 'Transform'.
///
///   When 'wl_output' is referred to from another module we can use the crate-
///   relative path 'crate::wl_output::Transform'.
///
/// Note we could always use the crate-relative path, but that would require
/// passing the 'interface' parameter around to lots of logic that otherwise
/// doesn't care.
fn enum_path(name: &str) -> String {
    let parts: Vec<&str> = name.splitn(2, ".").collect();
    if parts.len() == 1 {
        to_camel_case(name)
    } else {
        format!("crate::{}::{}", parts[0], to_camel_case(parts[1]))
    }
}

type ArgFormatterFn = for<'r> fn(&'r ast::Arg) -> Cow<'r, str>;

fn format_dispatch_arg_rust(arg: &ast::Arg) -> Cow<'_, str> {
    if let Some(ref enum_type) = arg.enum_type {
        return format!("Enum<{}>", enum_path(enum_type)).into();
    }
    match arg.kind {
        ArgKind::Int => "i32".into(),
        ArgKind::Uint => "u32".into(),
        ArgKind::Fixed => "Fixed".into(),
        ArgKind::String => "String".into(),
        ArgKind::Object => "ObjectId".into(),
        ArgKind::NewId => {
            if let Some(interface) = &arg.interface {
                format!("NewObject<{}>", to_camel_case(&interface)).into()
            } else {
                "ObjectId".into()
            }
        }
        ArgKind::Array => "Array".into(),
        ArgKind::Fd => "fuchsia_zircon::Handle".into(),
    }
}

fn format_wire_arg_rust(arg: &ast::Arg) -> Cow<'_, str> {
    if let Some(ref enum_type) = arg.enum_type {
        return enum_path(enum_type).into();
    }
    match arg.kind {
        ArgKind::Int => "i32",
        ArgKind::Uint => "u32",
        ArgKind::Fixed => "Fixed",
        ArgKind::String => "String",
        ArgKind::Object => "ObjectId",
        ArgKind::NewId => "NewId",
        ArgKind::Array => "Array",
        ArgKind::Fd => "fuchsia_zircon::Handle",
    }
    .into()
}

fn format_arg_kind(arg: &ast::Arg) -> &'static str {
    if arg.enum_type.is_some() {
        return "ArgKind::Uint";
    }
    match arg.kind {
        ArgKind::Int => "ArgKind::Int",
        ArgKind::Uint => "ArgKind::Uint",
        ArgKind::Fixed => "ArgKind::Fixed",
        ArgKind::String => "ArgKind::String",
        ArgKind::Object => "ArgKind::Object",
        ArgKind::NewId => "ArgKind::NewId",
        ArgKind::Array => "ArgKind::Array",
        ArgKind::Fd => "ArgKind::Handle",
    }
}

fn format_wire_arg(arg: &ast::Arg, var: &str) -> String {
    if arg.enum_type.is_some() {
        return format!("Arg::Uint({}.bits())", var);
    }
    match arg.kind {
        ArgKind::Int => format!("Arg::Int({})", var),
        ArgKind::Uint => format!("Arg::Uint({})", var),
        ArgKind::Fixed => format!("Arg::Fixed({})", var),
        ArgKind::String => format!("Arg::String({})", var),
        ArgKind::Object => format!("Arg::Object({})", var),
        ArgKind::NewId => format!("Arg::NewId({})", var),
        ArgKind::Array => format!("Arg::Array({})", var),
        ArgKind::Fd => format!("Arg::Handle({})", var),
    }
}

fn arg_to_primitive(arg: &ast::Arg) -> String {
    if let Some(ref enum_type) = arg.enum_type {
        return format!(
            "as_uint().map(|i| match {}::from_bits(i) {{
                                      Some(e) => Enum::Recognized(e),
                                      None => Enum::Unrecognized(i),
                                 }})?",
            enum_path(enum_type)
        );
    }
    match arg.kind {
        ArgKind::Int => "as_int()?",
        ArgKind::Uint => "as_uint()?",
        ArgKind::Fixed => "as_fixed()?.into()",
        ArgKind::String => "as_string()?",
        ArgKind::Object => "as_object()?",
        ArgKind::NewId => "as_new_id()?.into()",
        ArgKind::Array => "as_array()?",
        ArgKind::Fd => "as_handle()?",
    }
    .to_string()
}

/// Helper trait for transforming wayland protocol names into the rust
/// counterparts.
///
/// Ex, wl_display is written WlDisplay in rust code.
trait RustName {
    fn rust_name(&self) -> String;
}

impl RustName for ast::EnumEntry {
    // some wayland enums are just numbers, which would result in illegal rust
    // symbols. If we see a name that starts with a number we'll just prefix
    // with '_'.
    fn rust_name(&self) -> String {
        let is_digit = self.name.chars().next().map_or(false, |c| c.is_digit(10));
        let prefix = if is_digit { "_" } else { "" };
        format!("{}{}", prefix, to_camel_case(&self.name))
    }
}

fn is_rust_keyword(s: &str) -> bool {
    match s {
        "as" | "break" | "const" | "continue" | "crate" | "dyn" | "else" | "enum" | "extern"
        | "false" | "fn" | "for" | "if" | "impl" | "in" | "let" | "loop" | "match" | "mod"
        | "move" | "mut" | "pub" | "ref" | "return" | "Self" | "self" | "static" | "struct"
        | "super" | "trait" | "true" | "type" | "unsafe" | "use" | "where" | "while"
        | "abstract" | "async" | "await" | "become" | "box" | "do" | "final" | "macro"
        | "override" | "priv" | "try" | "typeof" | "unsized" | "virtual" | "yield" => true,
        _ => false,
    }
}

impl RustName for ast::Arg {
    fn rust_name(&self) -> String {
        if is_rust_keyword(&self.name) {
            format!("{}_", self.name)
        } else {
            self.name.to_owned()
        }
    }
}

impl RustName for ast::Message {
    fn rust_name(&self) -> String {
        to_camel_case(&self.name)
    }
}

impl RustName for ast::Enum {
    fn rust_name(&self) -> String {
        to_camel_case(&self.name)
    }
}

impl RustName for ast::Interface {
    fn rust_name(&self) -> String {
        to_camel_case(&self.name)
    }
}
