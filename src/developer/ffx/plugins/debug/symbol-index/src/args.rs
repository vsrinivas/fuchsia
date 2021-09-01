// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {argh::FromArgs, ffx_core::ffx_command};

#[ffx_command()]
#[derive(FromArgs, PartialEq, Debug)]
#[argh(
    subcommand,
    name = "symbol-index",
    description = "manages symbol-index",
    note = "symbol-index is a global configuration used by debugging tools to locate
symbol files."
)]
pub struct SymbolIndexCommand {
    #[argh(subcommand)]
    pub sub_command: SymbolIndexSubCommand,
}

#[derive(FromArgs, PartialEq, Debug)]
#[argh(subcommand)]
pub enum SymbolIndexSubCommand {
    List(ListCommand),
    Add(AddCommand),
    Remove(RemoveCommand),
    Clean(CleanCommand),
    Generate(GenerateCommand),
}

#[derive(FromArgs, PartialEq, Debug)]
#[argh(subcommand, name = "list", description = "show the content in symbol index")]
pub struct ListCommand {
    /// show the aggregated symbol index
    #[argh(switch, short = 'a')]
    pub aggregated: bool,
}

#[derive(FromArgs, PartialEq, Debug)]
#[argh(
    subcommand,
    name = "add",
    description = "add a path to the symbol index",
    note = "Add a path to the symbol index. The path could be
  - A build-id directory, with an optional build directory.
  - An ids.txt file, with an optional build directory.
  - A file that ends with .symbol-index.json.
  - A <package_name>.far file with a corresponding
    <package_name>.symbol-index.json.

Duplicated adding of the same path is a no-op, regardless of the optional
build directory."
)]
pub struct AddCommand {
    #[argh(option)]
    /// optional build directory used by zxdb to locate the source code
    pub build_dir: Option<String>,

    #[argh(positional)]
    /// the path to add
    pub path: String,
}

#[derive(FromArgs, PartialEq, Debug)]
#[argh(
    subcommand,
    name = "remove",
    description = "remove a path from the symbol index",
    note = "Remove a path from the symbol index. The path could be
  - A build-id directory.
  - An ids.txt file.
  - A file that ends with .symbol-index.json."
)]
pub struct RemoveCommand {
    #[argh(positional)]
    /// the path to remove
    pub path: String,
}

#[derive(FromArgs, PartialEq, Debug)]
#[argh(
    subcommand,
    name = "clean",
    description = "remove all non-existent paths",
    note = "Remove all non-existent paths from the symbol index, useful as a garbage
collection."
)]
pub struct CleanCommand {}

#[derive(FromArgs, PartialEq, Debug)]
#[argh(
    subcommand,
    name = "generate",
    description = "generate a <package_name>.symbol-index.json",
    note = "A helper that should be invoked by a build tool to generate a
<package_name>.symbol-index.json."
)]
pub struct GenerateCommand {
    /// output filename
    #[argh(option, short = 'o')]
    pub output: String,

    /// add a .build-id directory
    #[argh(option)]
    pub build_id_dir: Vec<String>,

    /// add an ids.txt file
    #[argh(option)]
    pub ids_txt: Vec<String>,

    /// add another symbol-index.json file
    #[argh(option)]
    pub symbol_index_json: Vec<String>,

    /// associated build_dir for all .build-id directories and ids.txt.
    #[argh(option)]
    pub build_dir: Option<String>,
}
