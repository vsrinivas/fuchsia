// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! This is a migration tool to help move from C-macro style bind rules to using the bind compiler.

mod composite_bind;
mod composite_device_desc;
pub mod library;

use regex::Regex;
use std::collections::HashSet;
use std::fmt::Write as FmtWrite;
use std::fs::File;
use std::fs::OpenOptions;
use std::io::{Read, Seek, SeekFrom, Write};
use std::path::PathBuf;
use structopt::StructOpt;

#[derive(StructOpt, Debug)]
struct Opt {
    #[structopt(parse(from_os_str))]
    input: PathBuf,
}

fn process_build_file(input: PathBuf) -> Result<Vec<PathBuf>, &'static str> {
    let mut output_path = input.clone();
    let mut file = File::open(input).map_err(|_| "Failed to open build file")?;
    let mut contents = String::new();
    file.read_to_string(&mut contents).map_err(|_| "Failed to read build file")?;

    let driver_module_re = Regex::new("driver_module").unwrap();
    let sources_re = Regex::new(r"sources = \[([^\]]*)\]").unwrap();

    let module =
        driver_module_re.find(&contents).ok_or("Couldn't find driver_module in build file")?;
    let sources = sources_re
        .captures(&contents[module.start()..])
        .ok_or("Couldn't find sources in driver_module target")?;

    let source_files = sources.get(1).ok_or("Couldn't find sources in driver_module target")?;
    let mut result = vec![];
    for source in source_files.as_str().split(",") {
        let trimmed = source.trim();
        let unquoted = trimmed.trim_matches('"');
        if !unquoted.is_empty() {
            output_path.set_file_name(unquoted);
            result.push(output_path.clone());
        }
    }
    Ok(result)
}

fn insert_build_rule(
    file_path: PathBuf,
    libraries: HashSet<library::Library>,
    device_name: &str,
) -> Result<(), &'static str> {
    let mut file = OpenOptions::new()
        .read(true)
        .write(true)
        .open(file_path)
        .map_err(|_| "Failed to open build file")?;
    let mut contents = String::new();
    file.read_to_string(&mut contents).map_err(|_| "Failed to read build file")?;

    // Find the import list and store the location in the BUILD file.
    let import_re = Regex::new(r"import\([^\)]*\)\n").unwrap();
    let mut iter = import_re.find_iter(&contents);
    let first_import = iter.next().ok_or("Couldn't find import list in build file")?;
    let last_import = iter.last().unwrap_or(first_import);

    // Check for a driver_module.
    let driver_module_re = Regex::new("driver_module").unwrap();
    let module =
        driver_module_re.find(&contents).ok_or("Couldn't find driver_module in build file")?;

    // Find the dependencies and store the location in the BUILD file.
    let deps_re = Regex::new(r"deps = \[").unwrap();
    let deps =
        deps_re.find(&contents[module.start()..]).ok_or("Couldn't find driver_module deps")?;
    let deps_start = module.start() + deps.start();
    let deps_end = module.start() + deps.end();

    let mut output = String::new();

    // Add the list of imports. Push the bind import if it's missing.
    output.push_str(&contents[..first_import.start()]);
    if !contents.contains("import(\"//build/bind/bind.gni\")") {
        output.push_str("import(\"//build/bind/bind.gni\")\n");
    }
    output.push_str(&contents[first_import.start()..last_import.end()]);
    output.push_str("\n");

    // Add the bind_rules.
    output.push_str(format!("driver_bind_rules(\"{}_bind\") {{\n", device_name).as_str());
    output.push_str(format!("  rules = \"{}.bind\"\n", device_name).as_str());
    output.push_str(format!("  header_output = \"{}_bind.h\"\n", device_name).as_str());
    output.push_str(format!("  bind_output = \"{}_bind.bc\"\n", device_name).as_str());
    if !libraries.is_empty() {
        output.push_str("  deps = [\n");
        for library in &libraries {
            output.push_str(format!("    \"{}\",\n", library.build_target()).as_str());
        }
        output.push_str("  ]\n");
    }
    output.push_str("}\n");

    // Add the bind header to the driver_module's dependency.
    output.push_str(&contents[last_import.end()..deps_start]);
    output.push_str(format!("deps = [\n    \":{}_bind_header\",", device_name).as_str());
    output.push_str(&contents[deps_end..]);

    file.seek(SeekFrom::Start(0)).map_err(|_| "Failed to seek to beginning of build file")?;
    file.set_len(0).map_err(|_| "Failed to truncate build file")?;
    file.write_all(output.as_bytes()).map_err(|_| "Failed to write back to build file")?;

    Ok(())
}

fn write_bind_rules(
    build_file_path: PathBuf,
    source_file_path: PathBuf,
    data: composite_bind::CompositeDeviceData,
) -> Result<(), &'static str> {
    let mut nodes_str = String::new();
    let mut primary_node_str = String::new();

    for node in data.nodes {
        let mut node_output = String::new();
        node_output
            .write_fmt(format_args!(
                include_str!("templates/composite_node.template"),
                node_name = node.name,
                instructions = node.bind_rules,
            ))
            .map_err(|_| "Failed to format output")?;

        if node.name.eq(&data.desc.primary_node) {
            primary_node_str = node_output;
        } else {
            nodes_str.push_str(&format!("\n{}", node_output));
        }
    }

    if primary_node_str.is_empty() {
        return Err("Missing primary node instructions");
    }

    let mut libraries_str = String::new();
    if !data.libraries.is_empty() {
        libraries_str.push_str("\n");
        for library in data.libraries.iter() {
            libraries_str.push_str(format!("using {};\n", library.name()).as_str());
        }
    }

    let mut output = String::new();
    output
        .write_fmt(format_args!(
            include_str!("templates/composite_bind.template"),
            libraries = libraries_str,
            device_name = str::replace(&data.desc.device_name, "-", "_"),
            primary_node = primary_node_str,
            additional_nodes = nodes_str,
        ))
        .map_err(|_| "Failed to format output")?;

    let mut bind_file_path = source_file_path;
    bind_file_path.set_file_name(format!("{}.bind", data.desc.device_name).as_str());
    let mut bind_file = OpenOptions::new()
        .read(true)
        .write(true)
        .create(true)
        .open(bind_file_path)
        .map_err(|_| "Failed to open bind file")?;
    bind_file.write_all(output.as_bytes()).map_err(|_| "Failed to write to bind file")?;

    // Insert the dependency in the BUILD file.
    insert_build_rule(build_file_path, data.libraries, &data.desc.device_name)
}

fn main() -> Result<(), &'static str> {
    let opt = Opt::from_iter(std::env::args());

    let mut migrate_success: Vec<String> = vec![];
    let mut migrate_failure: Vec<String> = vec![];

    for source_file in process_build_file(opt.input.clone())? {
        if let Some(source_file_str) = source_file.to_str() {
            println!("\nMigrating {}", source_file_str);
            let src_file_str = source_file_str.to_string();
            let result = composite_bind::process_source_file(source_file.clone())
                .and_then(|data| write_bind_rules(opt.input.clone(), source_file, data));
            match result {
                Err(e) => {
                    println!("Unable to migrate: {}", e);
                    migrate_failure.push(src_file_str);
                }
                Ok(()) => {
                    println!("Migration successful!");
                    migrate_success.push(src_file_str);
                }
            };
        }
    }

    println!("\nSuccessfully migrated: ");
    for file in migrate_success {
        println!("   {}", file);
    }

    println!("\nUnable to migrate: ");
    for file in migrate_failure {
        println!("   {}", file);
    }

    Ok(())
}
