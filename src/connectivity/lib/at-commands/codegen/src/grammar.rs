// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/// This module contains the pest grammar for AT Command definition DSL.
use pest_derive::Parser;

#[derive(Parser)]
#[grammar_inline = r##"

file = { SOI ~ definition* ~ EOI }
definition = { command | response | enumeration }

command = { "command" ~ "{" ~ ( read | test | execute ) ~ "}" }

read =    { "AT" ~ optional_extension ~ command_name ~ "?" }
test =    { "AT" ~ optional_extension ~ command_name ~ "=?" }
execute = { "AT" ~ optional_extension ~ command_name ~ execute_arguments? }

execute_arguments = { execute_argument_delimiter ~ arguments }
execute_argument_delimiter = { "=" | ">" }

response = { "response" ~ "{" ~ optional_extension ~ command_name ~ ":" ~ arguments ~ "}" }

optional_extension = { "+"? }

arguments = { parenthesized_argument_lists | argument_list }
parenthesized_argument_lists = { ("(" ~ argument_list ~ ")")+ }
argument_list = { (argument ~ ",")* ~ argument? }
argument = { identifier ~ ":" ~ typ }

typ = { list_type | map_type | identifier }
list_type = { "List" ~ "<" ~ identifier ~ ">" }
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
