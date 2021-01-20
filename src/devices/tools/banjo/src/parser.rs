// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use pest_derive::Parser;

#[derive(Parser)]
#[grammar_inline = r#"
COMMENT = _{ "//" ~ !("/") ~ (" " | SYMBOL | PUNCTUATION | ASCII_ALPHANUMERIC)* ~ "\n"}
WHITESPACE = _{ " " | "\n" | "\t" }
file = { SOI ~ contents ~ EOI }

contents = _{ library_header ~ (using_list)* ~ declaration_list }

library_header = { "library" ~ compound_ident ~ ";" }

using_list = _{ using | using_decl }
using_decl  = { "using" ~ compound_ident ~ "=" ~ primitive_type ~ ";" }
using = { "using" ~ compound_ident ~ ("as" ~ ident)? ~ ";" }

declaration_list = _{ ( declaration ~ ";" )* }

declaration = _{ resource_declaration | const_declaration | enum_declaration | union_declaration | struct_declaration | protocol_declaration | alias_declaration }

resource_declaration = { attributes ~ "deprecated_resource" ~ (handle_type | identifier_type) ~ (":" ~ constant ~ ("," ~ constant)*)? }

const_declaration = { attributes ~ "const" ~ ( primitive_type | string_type | identifier_type ) ~ ident ~ "=" ~ constant }

doc_comment = ${ "///" ~ (" " | SYMBOL | PUNCTUATION | ASCII_ALPHANUMERIC)* ~ "\n"}
doc_comment_block = _{ (doc_comment)* }
attribute = { ident ~ ( "=" ~ string)? }
attribute_list = { "[" ~ ( (attribute) ~ (",")? )* ~ "]" }
attributes = !{ (doc_comment_block)? ~ (attribute_list)? }

struct_declaration = { attributes ~ "resource"? ~ "struct" ~ ident ~ "{" ~ (struct_field ~ ";")* ~ "}" }
struct_field  = { attributes? ~ type_ ~ ident ~ ("=" ~ constant)? }

union_declaration = { attributes ~ "union" ~ ident ~ "{" ~ (union_field ~ ";")* ~ "}" }
union_field  = { attributes? ~ type_ ~ ident }

enum_declaration = { attributes ~ "enum" ~ ident ~ (":" ~ (integer_type | identifier_type))? ~ "{" ~ (enum_field ~ ";")* ~ "}" }
enum_field  = { attributes? ~ ident ~ "=" ~ constant }

protocol_method = { attributes ~ protocol_parameters }
protocol_parameters = { ident ~ parameter_list ~ ( "->" ~ parameter_list )? }

parameter_list = { "(" ~ ( parameters )? ~ ")" }
parameters = _{ parameter ~ "," ~ parameters | parameter }
docless_attributes_ = { attribute_list? }
parameter = { docless_attributes_ ~ type_ ~ ident }

super_protocol_list = { compound_ident | compound_ident ~ "," ~ super_protocol_list }

protocol_declaration = { attributes ~ "protocol" ~ ident ~ ( ":" ~ super_protocol_list )?
                          ~ "{" ~ ( protocol_method ~ ";" )*  ~ "}" }

alias_declaration = { attributes ~ "alias" ~ ident ~ "=" ~ ident }

type_ = _{ string_type | primitive_type | vector_type | array_type | handle_type | fidl_handle_type | identifier_type }

identifier_type = { compound_ident ~ reference? }
array_type = { "array" ~ "<" ~ type_ ~ ">" ~ ":" ~ constant }
vector_type = { "vector" ~ "<" ~ type_ ~ ">" ~ (":" ~ constant )? ~ reference? }

string_type = { "string" ~ ( ":" ~ constant )? ~ reference? }

primitive_type = { integer_type | "voidptr" | "bool" | "float32" | "float64" }

integer_type = { "usize" | "int8" | "int16" | "int32" | "int64" |
               "uint8" | "uint16" | "uint32" | "uint64" }

handle_type = { "handle" ~ ( "<" ~ handle_subtype ~ ">" )? ~ reference? }

handle_subtype = { "process" | "thread" | "vmo" | "channel" | "eventpair" | "port" |
                 "interrupt" | "log" | "socket" | "resource" | "event" |
                 "job" | "vmar" | "fifo" | "guest" | "timer" | "bti" | "profile" |
                 "debuglog" | "vcpu" | "iommu" | "pager" | "pmt" | "clock" |
                 "msi" }

fidl_handle_type = { "zx.handle:" ~ fidl_handle_subtype ~ reference? }

fidl_handle_subtype = { "VMO" }

compound_ident = ${ ident ~ ("." ~ ident)* }
ident = @{ ("@")? ~ (alpha | digit | "_")+ }
alpha = { 'a'..'z' | 'A'..'Z' }
digit = { '0'..'9' }
string = { "\"" ~ ("-" | "_" | "/" | "." | "," | "'" | "(" | ")" | "&" | "*" |
           SYMBOL | alpha | digit)* ~ "\"" }
numeric = { ("-" | digit)* }
constant = { compound_ident | string | numeric }
reference = { "?" }
"#]
pub struct BanjoParser;
