// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::ast::{Message, Service, ServiceSet, Structure, SubParam, TLV};
use failure::Error;
use std::io;

pub struct Codegen<'a, W: io::Write> {
    w: &'a mut W,
    depth: usize,
}

fn type_fmt(ty: Option<u16>, optional: bool) -> String {
    let ty = match ty {
        Some(size) => match size {
            1 => "u8",
            2 => "u16",
            4 => "u32",
            _ => panic!("Invalid size to type conversion: {}", size),
        },
        None => "String",
    }
    .to_string();

    if optional {
        format!("Option<{}>", ty)
    } else {
        ty
    }
}

macro_rules! indent {
    ($depth:expr) => {
        $depth.depth += 4;
    };
}

macro_rules! dedent {
    ($depth:expr) => {
        $depth.depth -= 4;
    };
}

macro_rules! write_indent {
    ($dst:expr, $($arg:tt)*) => (
        $dst.w.write_fmt(format_args!("{space:>indent$}", indent = $dst.depth, space=""))?;
        $dst.w.write_fmt(format_args!($($arg)*))?
    )
}
macro_rules! writeln_indent {
    ($dst:expr, $($arg:tt)*) => (
        write_indent!($dst, $($arg)*);
        $dst.w.write_fmt(format_args!("\n"))?
    )
}

impl<'a, W: io::Write> Codegen<'a, W> {
    pub fn new(w: &'a mut W) -> Codegen<'a, W> {
        Codegen { w, depth: 0 }
    }

    pub fn codegen_svc_tlv_encode(
        &mut self,
        field: &TLV,
        _structure: &Structure,
    ) -> Result<(), Error> {
        if field.optional {
            writeln_indent!(self, "if let Some(ref {}) = self.{} {{", field.param, field.param);
            indent!(self);
        } else {
            if field.has_sub_params() {
                // TODO check the subparam size is equal to the whole type
                for param in field.sub_params.iter() {
                    writeln_indent!(self, "let {} = &self.{};", param.param, param.param);
                }
            } else {
                writeln_indent!(self, "let {} = &self.{};", field.param, field.param);
            }
        }

        // service msg type
        writeln_indent!(self, "buf.put_u8({});", field.id);

        match field.size {
            None => {
                // service msg length (variable)
                writeln_indent!(self, "buf.put_u16_le({}.as_bytes().len() as u16);", field.param);
                // service msg value
                writeln_indent!(self, "buf.put_slice({}.as_bytes());", field.param);
            }
            Some(size) => {
                // service msg length (fixed size)
                writeln_indent!(self, "buf.put_u16_le({});", size);
                // service msg value
                if field.has_sub_params() {
                    for param in field.sub_params.iter() {
                        match param.size {
                            1 => {
                                writeln_indent!(self, "buf.put_u8(*{});", param.param);
                            }
                            2 => {
                                writeln_indent!(self, "buf.put_u16_le(*{});", param.param);
                            }
                            4 => {
                                writeln_indent!(self, "buf.put_u32_le(*{});", param.param);
                            }
                            _ => panic!("invalid field size: {}", size),
                        }
                    }
                } else {
                    match size {
                        1 => {
                            writeln_indent!(self, "buf.put_u8(*{});", field.param);
                        }
                        2 => {
                            writeln_indent!(self, "buf.put_u16_le(*{});", field.param);
                        }
                        4 => {
                            writeln_indent!(self, "buf.put_u32_le(*{});", field.param);
                        }
                        _ => panic!("invalid field size: {}", size),
                    }
                }
            }
        }

        if field.optional {
            dedent!(self);
            writeln_indent!(self, "}}");
        }

        Ok(())
    }

    pub fn codegen_svc_decode_subfield(&mut self, field: &SubParam) -> Result<(), Error> {
        match field.size {
            1 => {
                writeln_indent!(self, "{} = buf.get_u8();", field.param);
            }
            2 => {
                writeln_indent!(self, "{} = buf.get_u16_le();", field.param);
            }
            4 => {
                writeln_indent!(self, "{} = buf.get_u32_le();", field.param);
            }
            _ => panic!("invalid field size: {}", field.size),
        }
        Ok(())
    }

    pub fn codegen_svc_decode_field(&mut self, field: &TLV) -> Result<(), Error> {
        match field.size {
            None => {
                writeln_indent!(self, "let dst = buf.by_ref().take(tlv_len as usize).collect();");
                writeln_indent!(self, "total_len -= tlv_len;");
                if field.optional {
                    writeln_indent!(
                        self,
                        "{} = Some(String::from_utf8(dst).unwrap());",
                        field.param
                    );
                } else {
                    writeln_indent!(self, "{} = String::from_utf8(dst).unwrap();", field.param);
                }
            }
            Some(size) => {
                match size {
                    1 => {
                        if field.optional {
                            writeln_indent!(self, "{} = Some(buf.get_u8());", field.param);
                        } else {
                            writeln_indent!(self, "{} = buf.get_u8();", field.param);
                        }
                        writeln_indent!(self, "total_len -= 1;");
                    }
                    2 => {
                        if field.has_sub_params() {
                            // TODO check the subparam size is equal to the whole type
                            for param in field.sub_params.iter() {
                                self.codegen_svc_decode_subfield(param)?;
                            }
                        } else {
                            if field.optional {
                                writeln_indent!(self, "{} = Some(buf.get_u16_le());", field.param);
                            } else {
                                writeln_indent!(self, "{} = buf.get_u16_le();", field.param);
                            }
                        }
                        writeln_indent!(self, "total_len -= 2;");
                    }
                    4 => {
                        if field.has_sub_params() {
                            // TODO check the subparam size is equal to the whole type
                            for param in field.sub_params.iter() {
                                self.codegen_svc_decode_subfield(param)?;
                            }
                        } else {
                            if field.optional {
                                writeln_indent!(self, "{} = Some(buf.get_u32_le());", field.param);
                            } else {
                                writeln_indent!(self, "{} = buf.get_u32_le();", field.param);
                            }
                            writeln_indent!(self, "total_len -= 4;");
                        }
                    }
                    _ => panic!("invalid field size: {}", size),
                }
            }
        }
        Ok(())
    }

    pub fn codegen_svc_decode(
        &mut self,
        _svc_id: u16,
        msg: &Message,
        _structure: &Structure,
    ) -> Result<(), Error> {
        // define the request struct
        writeln_indent!(self, "impl Decodable for {}Resp {{", msg.name);
        indent!(self);
        // transaction_id_len fn
        writeln_indent!(
            self,
            "fn from_bytes<T: Buf>(mut buf: T) -> Result<{}Resp, QmiError> {{",
            msg.name
        );
        indent!(self);

        // parse the result tlv
        writeln_indent!(self, "assert_eq!({}, buf.get_u16_le());", msg.ty);
        writeln_indent!(self, "let mut total_len = buf.get_u16_le();");
        writeln_indent!(self, "let _ = buf.get_u8();");
        writeln_indent!(self, "total_len -= 1;");
        writeln_indent!(self, "let res_len = buf.get_u16_le();");
        writeln_indent!(self, "total_len -= res_len + 2;");

        writeln_indent!(self, "if 0x00 != buf.get_u16_le() {{");
        // This is an error case, generate a qmi error
        indent!(self);
        writeln_indent!(self, "let error_code = buf.get_u16_le();");
        writeln_indent!(self, "return Err(QmiError::from_code(error_code))");
        dedent!(self);
        writeln_indent!(self, "}} else {{");
        indent!(self);
        writeln_indent!(
            self,
            "assert_eq!(0x00, buf.get_u16_le()); // this must be zero if no error from above check"
        );
        dedent!(self);
        writeln_indent!(self, "}}");

        for field in msg.get_response_fields() {
            if field.has_sub_params() {
                for param in field.sub_params.iter() {
                    writeln_indent!(self, "let mut {} = Default::default();", param.param);
                }
            } else {
                writeln_indent!(self, "let mut {} = Default::default();", field.param);
            }
        }
        writeln_indent!(self, "while total_len > 0 {{");
        indent!(self);
        writeln_indent!(self, "let msg_id = buf.get_u8();");
        writeln_indent!(self, "total_len -= 1;");
        writeln_indent!(self, "let tlv_len = buf.get_u16_le();");
        writeln_indent!(self, "total_len -= 2;");
        writeln_indent!(self, "match msg_id {{");
        indent!(self);
        for field in msg.get_response_fields() {
            writeln_indent!(self, "{} => {{", field.id);
            indent!(self);
            self.codegen_svc_decode_field(field)?;
            dedent!(self);
            writeln_indent!(self, "}}");
        }
        writeln_indent!(self, "0 => {{ eprintln!(\"Found a type of 0, modem gave a bad TLV, trying to recover\"); break; }}");
        writeln_indent!(self, "e_code => {{");
        indent!(self);
        writeln_indent!(self, "eprintln!(\"Unknown id for this message type: {{}}, removing {{}} of len\", e_code, tlv_len);");
        writeln_indent!(self, "total_len -= tlv_len;");
        dedent!(self);
        writeln_indent!(self, "}}");
        dedent!(self);
        writeln_indent!(self, "}}");
        dedent!(self);
        writeln_indent!(self, "}}");

        writeln_indent!(self, "Ok({}Resp {{", msg.name);
        for field in msg.get_response_fields() {
            indent!(self);
            if field.has_sub_params() {
                for param in field.sub_params.iter() {
                    writeln_indent!(self, "{},", param.param);
                }
            } else {
                writeln_indent!(self, "{},", field.param);
            }
            dedent!(self);
        }
        writeln_indent!(self, "}})");

        dedent!(self);
        writeln_indent!(self, "}}");

        dedent!(self);
        writeln_indent!(self, "}}");
        Ok(())
    }
    pub fn codegen_svc_encode(
        &mut self,
        svc_id: u16,
        msg: &Message,
        structure: &Structure,
    ) -> Result<(), Error> {
        // define the request struct
        writeln_indent!(self, "impl Encodable for {}Req {{", msg.name);
        indent!(self);
        // transaction_id_len fn
        writeln_indent!(self, "fn transaction_id_len(&self) -> u8 {{");
        indent!(self);
        writeln_indent!(self, "{}", structure.transaction_len);
        dedent!(self);
        writeln_indent!(self, "}}");

        // svc_id fn
        writeln_indent!(self, "fn svc_id(&self) -> u8 {{");
        indent!(self);
        writeln_indent!(self, "{}", svc_id);
        dedent!(self);
        writeln_indent!(self, "}}");

        // to_bytes fn
        writeln_indent!(self, "fn to_bytes(&self) -> (Bytes, u16) {{");
        indent!(self);
        writeln_indent!(self, "let mut buf = BytesMut::with_capacity(MSG_MAX);");

        // Service headers

        // Service Type id
        writeln_indent!(self, "// svc id");
        writeln_indent!(self, "buf.put_u16_le({});", msg.ty);

        // calculate the length of the service message (including variable tlvs)
        writeln_indent!(self, "// svc length calculation");
        writeln_indent!(self, "let mut {}_len = 0u16;", msg.name);
        for field in msg.get_request_fields() {
            let field_name = match field.size {
                Some(_) => format!("_{}", field.param),
                None => field.param.clone(),
            };
            if field.optional {
                writeln_indent!(self, "if let Some(ref {}) = self.{} {{", field_name, field.param);
                indent!(self);
                writeln_indent!(self, "{}_len += 1; // tlv type length;", msg.name);
                writeln_indent!(self, "{}_len += 2; // tlv length length;", msg.name);
            } else {
                writeln_indent!(self, "{}_len += 1; // tlv type length;", msg.name);
                writeln_indent!(self, "{}_len += 2; // tlv length length;", msg.name);
                writeln_indent!(self, "let {} = &self.{};", field_name, field.param);
            }
            if let Some(size) = field.size {
                writeln_indent!(self, "{}_len += {};", msg.name, size);
            } else {
                writeln_indent!(
                    self,
                    "{}_len += {}.as_bytes().len() as u16;",
                    msg.name,
                    field_name
                );
            }
            if field.optional {
                dedent!(self);
                writeln_indent!(self, "}}");
            }
        }
        writeln_indent!(self, "buf.put_u16_le({}_len);", msg.name);

        // Service SDU
        writeln_indent!(self, "// tlvs");
        for field in msg.get_request_fields() {
            self.codegen_svc_tlv_encode(field, structure)?;
        }

        writeln_indent!(
            self,
            "return (buf.freeze(), {}_len + 2 /*msg id len*/ + 2 /*msg len len*/);",
            msg.name
        );
        // close fn
        dedent!(self);
        writeln_indent!(self, "}}");
        // close impl
        dedent!(self);
        writeln_indent!(self, "}}");
        Ok(())
    }

    pub fn codegen_svc_msg(&mut self, msg: &Message) -> Result<(), Error> {
        // define the request struct
        writeln_indent!(self, "pub struct {}Req {{", msg.name);
        indent!(self);
        for field in msg.get_request_fields() {
            if field.has_sub_params() {
                for param in field.sub_params.iter() {
                    writeln_indent!(
                        self,
                        "pub {param}: {ty},",
                        param = param.param,
                        ty = type_fmt(Some(param.size), field.optional)
                    );
                }
            } else {
                writeln_indent!(
                    self,
                    "pub {param}: {ty},",
                    param = field.param,
                    ty = type_fmt(field.size, field.optional)
                );
            }
        }
        dedent!(self);
        writeln_indent!(self, "}}");

        // define the request struct impl
        writeln_indent!(self, "impl {}Req {{", msg.name);
        indent!(self);
        write_indent!(self, "pub fn new(");
        for field in msg.get_request_fields() {
            if field.has_sub_params() {
                for param in field.sub_params.iter() {
                    write!(
                        self.w,
                        "{}: {},",
                        param.param,
                        type_fmt(Some(param.size), field.optional)
                    )?;
                }
            } else {
                write!(self.w, "{}: {},", field.param, type_fmt(field.size, field.optional))?;
            }
        }
        writeln!(self.w, ") -> Self {{")?;
        indent!(self);
        writeln_indent!(self, "{}Req {{", msg.name);
        indent!(self);
        for field in msg.get_request_fields() {
            if field.has_sub_params() {
                for param in field.sub_params.iter() {
                    writeln_indent!(self, "{},", param.param);
                }
            } else {
                writeln_indent!(self, "{},", field.param);
            }
        }
        dedent!(self);
        writeln_indent!(self, "}}");

        // close impl
        dedent!(self);
        writeln_indent!(self, "}}");

        // close struct
        dedent!(self);
        writeln_indent!(self, "}}");

        // define the response struct
        writeln_indent!(self, "#[derive(Debug)]");
        writeln_indent!(self, "pub struct {}Resp {{", msg.name);
        indent!(self);
        for field in msg.get_response_fields() {
            if field.has_sub_params() {
                for param in field.sub_params.iter() {
                    writeln_indent!(
                        self,
                        "pub {param}: {ty},",
                        param = param.param,
                        ty = type_fmt(Some(param.size), false)
                    );
                }
            } else {
                writeln_indent!(
                    self,
                    "pub {param}: {ty},",
                    param = field.param,
                    ty = type_fmt(field.size, field.optional)
                );
            }
        }
        dedent!(self);
        writeln_indent!(self, "}}");

        Ok(())
    }

    pub fn codegen_service(&mut self, svc: &Service, structure: &Structure) -> Result<(), Error> {
        writeln!(self.w, "pub mod {} {{", svc.name)?;
        indent!(self);
        writeln_indent!(self, "use crate::{{Decodable, Encodable}};");
        writeln_indent!(self, "use crate::QmiError;");
        writeln_indent!(self, "use bytes::{{Bytes, Buf, BufMut, BytesMut}};");
        writeln_indent!(self, "const MSG_MAX: usize = 512;");
        for msg in svc.get_messages() {
            self.codegen_svc_msg(msg)?;
            self.codegen_svc_encode(svc.ty, msg, &structure)?;
            self.codegen_svc_decode(svc.ty, msg, &structure)?;
        }
        dedent!(self);

        writeln!(self.w, "}}")?;
        Ok(())
    }

    pub fn codegen(&mut self, svc_set: ServiceSet) -> Result<(), Error> {
        writeln!(
            self.w,
            "// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![allow(unused_mut, non_snake_case)]
use failure::Fail;
use bytes::{{Bytes, Buf}};
use std::fmt::Debug;
use std::result;

pub type QmiResult<T> = result::Result<T, QmiError>;

pub trait Encodable {{
    fn to_bytes(&self) -> (Bytes, u16);

    fn transaction_id_len(&self) -> u8;

    fn svc_id(&self) -> u8;
}}

pub trait Decodable {{
    fn from_bytes<T: Buf + Debug>(b: T) -> QmiResult<Self> where Self: Sized;
}}

"
        )?;
        // codegen error types
        writeln_indent!(self, "#[derive(Debug, Fail)]");
        writeln_indent!(self, "pub enum QmiError {{");
        indent!(self);
        for (error_type, error_set) in &svc_set.results {
            writeln_indent!(self, "// {} error types", error_type);
            for (name, code) in error_set {
                writeln_indent!(self, "#[fail(display = \"Qmi Result Error Code: {:#X}\")]", code);
                writeln_indent!(self, "{},", name);
            }
        }
        dedent!(self);
        writeln_indent!(self, "}}");

        writeln_indent!(self, "impl QmiError {{");
        indent!(self);
        writeln_indent!(self, "pub fn from_code(code: u16) -> Self {{");
        indent!(self);
        writeln_indent!(self, "match code {{");
        indent!(self);
        for (error_type, error_set) in &svc_set.results {
            writeln_indent!(self, "// {} error type", error_type);
            for (name, code) in error_set {
                writeln_indent!(self, "{} => QmiError::{},", code, name);
            }
        }
        writeln_indent!(self, "_c => panic!(\"Unknown Error Code: {{}}\", _c),");
        dedent!(self);
        writeln_indent!(self, "}}");
        dedent!(self);
        writeln_indent!(self, "}}");
        dedent!(self);
        writeln_indent!(self, "}}");

        // codegen service modules
        for service in svc_set.get_services() {
            self.codegen_service(service, svc_set.get_structure(&service.message_structure)?)?;
        }

        Ok(())
    }
}
