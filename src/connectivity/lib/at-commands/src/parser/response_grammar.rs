// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/// This module contains the pest grammar for AT command responses.
use pest_derive::Parser;

#[derive(Parser)]
#[grammar_inline = r##"

input = { SOI ~ response ~ EOI }

response = { ok | error | hardcoded_error | cme_error | success }

ok = { "OK" }

error = { "ERROR" }

hardcoded_error = {
    "NO CARRIER" |
    "BUSY" |
    "NO ANSWER" |
    "DELAYED" |
    "BLACKLIST"
}

cme_error = { "+CME ERROR:" ~ integer }

success = { optional_extension ~ command_name ~ ":" ~ arguments }

optional_extension = { "+"? }

arguments = { parenthesized_argument_lists | argument_list }
parenthesized_argument_lists = { ("(" ~ argument_list ~ ")")+ }
argument_list = { (argument ~ ",")* ~ argument? }
argument = { key_value_argument | primitive_argument }
key_value_argument = { primitive_argument ~ "=" ~ primitive_argument }
primitive_argument = { integer | string }

command_name = @{ ASCII_ALPHA_UPPER+ }
string = @{ (!(WHITE_SPACE | "," | "=" | ")") ~ ASCII)+ }
integer = @{ ASCII_DIGIT+ }

WHITESPACE = _{ WHITE_SPACE }
COMMENT = _{ "#" ~ (!NEWLINE ~ ANY)* ~ NEWLINE }

"##]

pub struct Grammar {}
