// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use serde_json::Value;

/// Commandline args for the validator
#[derive(argh::FromArgs)]
struct Args {
    /// schema file; must conform to draft 7 of JSON schema
    #[argh(positional)]
    schema: String,

    /// input file
    #[argh(positional)]
    input: String,

    /// stamp file
    #[argh(positional)]
    stamp_file: Option<String>,

    /// whether to parse the schema and input as JSON5
    #[argh(switch)]
    json5: bool,
}

// Ensures that the schema uses draft 7, which is the only version supported by valico.
fn check_schema_version(schema: &Value) {
    const SCHEMA_URL: &'static str = "http://json-schema.org/draft-07/schema#";

    if let Value::Object(map) = schema {
        if let Some(Value::String(url)) = map.get("$schema") {
            if url != SCHEMA_URL {
                eprintln!(
                    "Only JSON Schema Draft 7 is supported; \"$schema\" must equal \"{}\"",
                    SCHEMA_URL
                );
                std::process::exit(1);
            }
        }
    } else {
        eprintln!("Schema must be an object.");
        std::process::exit(1);
    }
}

fn main() {
    let args: Args = argh::from_env();

    let parse_fn = |filename| -> serde_json::Value {
        let contents = std::fs::read_to_string(&filename).unwrap();

        if args.json5 {
            serde_json5::from_str(&contents).expect(&format!("Failed to parse file {}", filename))
        } else {
            serde_json::from_str(&contents).expect(&format!("Failed to parse file {}", filename))
        }
    };

    let input = parse_fn(&args.input);
    let schema = parse_fn(&args.schema);
    check_schema_version(&schema);

    let mut scope = valico::json_schema::Scope::new();
    let schema = scope
        .compile_and_return(schema, false)
        .expect(&format!("Schema file {} is invalid", args.schema));

    let state = schema.validate(&input);
    if !state.is_valid() {
        eprintln!("Validation failed; error(s):");
        for error in state.errors {
            eprintln!(
                "  Path: {}; code: {}; description: {}",
                error.get_path(),
                error.get_code(),
                error.get_title()
            );
        }
        std::process::exit(1);
    }

    if let Some(stamp_file) = args.stamp_file {
        std::fs::File::create(&stamp_file).unwrap();
    }
}
