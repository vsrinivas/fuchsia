// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::io;

use crate::ast;
use crate::parser::ArgKind;

pub type Result = io::Result<()>;

pub struct Codegen<W: io::Write> {
    w: W,
}

impl<W: io::Write> Codegen<W> {
    pub fn new(w: W) -> Codegen<W> {
        Codegen { w }
    }

    pub fn codegen(&mut self, protocol: ast::Protocol) -> Result {
        self.codegen_protocol(protocol)
    }

    fn codegen_protocol(&mut self, protocol: ast::Protocol) -> Result {
        writeln!(
            self.w,
            "\
// GENERATED FILE -- DO NOT EDIT

use failure;
use \
             fuchsia_wayland_core::{{ArgKind, Arg, FromArgs, IntoMessage, Message,
                            MessageGroupSpec, MessageHeader, MessageSpec, NewId,
                            ObjectId, EncodeError, DecodeError,
                            Interface }};"
        )?;

        for interface in protocol.interfaces.into_iter() {
            let interface_name = to_camel_case(&interface.name);
            let request_type_name = format!("{}Request", interface_name);
            let event_type_name = format!("{}Event", interface_name);
            self.codegen_message_enum(
                &request_type_name,
                &interface.requests,
                format_dispatch_arg_rust,
            )?;
            self.codegen_message_enum(&event_type_name, &interface.events, format_wire_arg_rust)?;

            self.codegen_impl_event(&event_type_name, &interface.events)?;
            self.codegen_from_args(&request_type_name, &interface.requests)?;

            self.codegen_interface_trait(&interface)?;
        }
        Ok(())
    }

    /// Emits an enum that describes the set of messages for a single interface.
    /// Each interface will have a message enum for both Reqests and Events.
    ///
    /// Ex:
    ///  pub enum MyInterfaceRequest {
    ///    Request1 { arg1: u32 },
    ///    Request2 { name: String},
    ///  }
    fn codegen_message_enum<F: FnMut(&ast::Arg) -> &'static str>(
        &mut self, name: &str, messages: &Vec<ast::Message>, arg_formatter: F,
    ) -> Result {
        writeln!(self.w, "#[derive(Debug)]");
        writeln!(self.w, "pub enum {enum_name} {{", enum_name = name)?;
        for message in messages.iter() {
            let fields = message
                .args
                .iter()
                .map(|arg| {
                    format!(
                        "{arg_name}: {arg_type}",
                        arg_name = arg.name,
                        arg_type = arg_formatter(&arg)
                    )
                }).collect::<Vec<String>>()
                .join(", ");
            writeln!(
                self.w,
                "    {enum_variant} {{ {enum_fields} }},",
                enum_variant = to_camel_case(&message.name),
                enum_fields = fields
            )?;
        }
        writeln!(self.w, "}}")?;
        Ok(())
    }

    /// Generates an impl for the Event trait for a set of messages. This
    /// will be the code that allows the message type to be serialized into
    /// a Message that can be sent over channel.
    ///
    /// Ex:
    ///   impl IntoMessage for MyInterfaceEvent {
    ///       fn into_message(self, id: u32) -> Result<Message, Self::Error> {
    ///           let mut header = MessageHeader {...};
    ///           let mut message = Message::new();
    ///           message.write_header(&header);
    ///           match self {
    ///           MyInterfaceEvent::Event1 { uint_arg } => {
    ///               message.write_arg(Arg::Uint(uint_arg))?;
    ///               header.opcode = 0;
    ///           },
    ///           // ... Encode other events...
    ///           }
    ///
    ///           // Rewrite header with proper ordinal & length.
    ///           header.length = msg.bytes().len() as u16;
    ///           message.rewind();
    ///           message.write_header(&header);
    ///       }
    ///   }
    fn codegen_impl_event(&mut self, for_type: &str, messages: &Vec<ast::Message>) -> Result {
        write!(
            self.w,
            "\
impl IntoMessage for {target_type} {{
    type Error = EncodeError;
    fn \
             into_message(self, id: u32) -> Result<Message, Self::Error> {{
        let mut \
             header = MessageHeader {{
            sender: id,
            opcode: 0,
            length: 0,
        }};
        let mut msg = Message::new();
        msg.write_header(&header)?;
        match self {{",
            target_type = for_type
        )?;

        for (op, event) in messages.iter().enumerate() {
            write!(
                self.w,
                "
        {self_type}::{message_name} {{\n",
                self_type = for_type,
                message_name = to_camel_case(&event.name)
            )?;
            for arg in event.args.iter() {
                write!(self.w, "            {arg_name},\n", arg_name = arg.name)?;
            }
            write!(self.w, "        }} => {{\n")?;
            for arg in event.args.iter() {
                write!(
                    self.w,
                    "            msg.write_arg({arg})?;\n",
                    arg = format_wire_arg(&arg, &arg.name)
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

    fn codegen_from_args(&mut self, for_type: &str, messages: &Vec<ast::Message>) -> Result {
        write!(
            self.w,
            "\
impl FromArgs for {target_type} {{
    fn from_args(op: u16, mut args: \
             Vec<Arg>) -> Result<Self, failure::Error> {{
        match op {{",
            target_type = for_type
        )?;

        for (op, message) in messages.iter().enumerate() {
            write!(
                self.w,
                "
        {opcode} /* {op_name} */ => {{
            let mut iter = \
                 args.into_iter();
            Ok({self_type}::{message_name} {{\n",
                opcode = op,
                op_name = message.name,
                self_type = for_type,
                message_name = to_camel_case(&message.name)
            )?;
            for arg in message.args.iter() {
                writeln!(
                    self.w,
                    "                {}: iter.next()
                                             .ok_or(DecodeError::InsufficientArgs)?
                                             .{}?,",
                    arg.name,
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
    fn codegen_interface_trait(&mut self, interface: &ast::Interface) -> Result {
        let camel_name = to_camel_case(&interface.name);
        writeln!(self.w, "pub struct {};", camel_name)?;
        writeln!(self.w, "")?;
        writeln!(self.w, "impl Interface for {} {{", camel_name)?;
        writeln!(
            self.w,
            "    const NAME: &'static str = \"{}\";",
            interface.name
        )?;
        writeln!(self.w, "    const VERSION: u32 = {};", interface.version)?;
        write!(self.w, "    const REQUESTS: MessageGroupSpec = ")?;
        self.codegen_message_group_spec(&interface.requests)?;
        write!(self.w, "    const EVENTS: MessageGroupSpec = ")?;
        self.codegen_message_group_spec(&interface.events)?;
        writeln!(self.w, "    type Request = {}Request;", camel_name)?;
        writeln!(self.w, "    type Event = {}Event;", camel_name)?;
        writeln!(self.w, "}}")?;
        writeln!(self.w, "")?;
        Ok(())
    }

    fn codegen_message_group_spec(&mut self, messages: &Vec<ast::Message>) -> Result {
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
}

fn to_camel_case(s: &str) -> String {
    s.split('_')
        .filter(|s| s.len() > 0)
        .map(|s| s[..1].to_uppercase() + &s[1..])
        .collect()
}

fn format_dispatch_arg_rust(arg: &ast::Arg) -> &'static str {
    match arg.kind {
        ArgKind::Int => "i32",
        ArgKind::Uint => "u32",
        ArgKind::Fixed => "u32",
        ArgKind::String => "String",
        ArgKind::Object => "ObjectId",
        ArgKind::NewId => "ObjectId",
        ArgKind::Array => "Vec<u8>",
        ArgKind::Fd => "fuchsia_zircon::Handle",
    }
}

fn format_wire_arg_rust(arg: &ast::Arg) -> &'static str {
    match arg.kind {
        ArgKind::Int => "i32",
        ArgKind::Uint => "u32",
        ArgKind::Fixed => "u32",
        ArgKind::String => "String",
        ArgKind::Object => "ObjectId",
        ArgKind::NewId => "NewId",
        ArgKind::Array => "Vec<u8>",
        ArgKind::Fd => "fuchsia_zircon::Handle",
    }
}

fn format_arg_kind(arg: &ast::Arg) -> &'static str {
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

fn arg_to_primitive(arg: &ast::Arg) -> &'static str {
    match arg.kind {
        ArgKind::Int => "as_int()",
        ArgKind::Uint => "as_uint()",
        ArgKind::Fixed => "as_fixed()",
        ArgKind::String => "as_string()",
        ArgKind::Object => "as_object()",
        ArgKind::NewId => "as_new_id()",
        ArgKind::Array => "as_array()",
        ArgKind::Fd => "as_handle()",
    }
}
