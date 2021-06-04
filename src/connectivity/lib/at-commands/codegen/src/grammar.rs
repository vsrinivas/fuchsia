// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/// This module contains the pest grammar for AT Command definition DSL.
use pest_derive::Parser;

#[derive(Parser)]
#[grammar_inline = r##"

file = { SOI ~ definition* ~ EOI }
definition = { command | response | enumeration }

command = { "command" ~ optional_type_name ~ "{" ~ ( read | test | execute ) ~ "}" }

read =    { "AT" ~ optional_extension ~ command_name ~ "?" }
test =    { "AT" ~ optional_extension ~ command_name ~ "=?" }
execute = { "AT" ~ optional_extension ~ command_name ~ delimited_arguments? }

response = { "response" ~ optional_type_name ~ "{" ~ optional_extension ~ command_name ~ delimited_arguments ~ "}" }

delimited_arguments = { optional_delimited_argument_delimiter ~ arguments ~ optional_delimited_argument_terminator}
optional_delimited_argument_delimiter = { ("=" | ">" | ":")? }
optional_delimited_argument_terminator = { ";"? }

optional_type_name = { identifier? }

optional_extension = { "+"? }

arguments = { parenthesized_argument_lists | argument_list }
parenthesized_argument_lists = { ("(" ~ argument_list ~ ")")+ }
argument_list = { (argument ~ ",")* ~ argument? }
argument = { identifier ~ ":" ~ typ }

typ = { list_type | map_type | possibly_option_type }
possibly_option_type = { option_type | identifier }
option_type = { "Option" ~ "<" ~ identifier ~ ">" }
list_type = { "List" ~ "<" ~ possibly_option_type ~ ">" }
map_type = { "Map" ~ "<" ~ identifier  ~ "," ~ identifier ~ ">" }

enumeration = { "enum" ~ identifier ~ "{" ~ variants ~ "}" }

variants = { (variant ~ ",")* ~ variant? }
variant = { identifier ~ "=" ~ integer }

command_name = @{ ASCII_ALPHA_UPPER+ }
identifier = @{ (ASCII_ALPHANUMERIC | "_")+ }
integer = @{ ASCII_DIGIT+ }

WHITESPACE = _{ WHITE_SPACE }
COMMENT = _{ "#" ~ (!NEWLINE ~ ANY)* ~ NEWLINE }

"##]

pub struct Grammar {}
