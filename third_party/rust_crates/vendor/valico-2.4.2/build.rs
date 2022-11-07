extern crate phf_codegen;

use std::fs::File;
use std::io::{BufWriter, Write};
use std::path::Path;
use std::env;

fn main() {
    let path = Path::new(&env::var("OUT_DIR").unwrap()).join("codegen.rs");
    let mut file = BufWriter::new(File::create(&path).unwrap());

    write!(&mut file, "static PROPERTY_KEYS: phf::Set<&'static str> = ").unwrap();
    phf_codegen::Set::new()
        .entry("properties")
        .entry("patternProperties")
        .build(&mut file)
        .unwrap();
    write!(&mut file, ";\n").unwrap();

    write!(&mut file, "static NON_SCHEMA_KEYS: phf::Set<&'static str> = ").unwrap();
    phf_codegen::Set::new()
        .entry("properties")
        .entry("patternProperties")
        .entry("dependencies")
        .entry("definitions")
        .entry("anyOf")
        .entry("allOf")
        .entry("oneOf")
        .build(&mut file)
        .unwrap();
    write!(&mut file, ";\n").unwrap();

    write!(&mut file, "static FINAL_KEYS: phf::Set<&'static str> = ").unwrap();
    phf_codegen::Set::new()
        .entry("enum")
        .entry("required")
        .entry("type")
        .build(&mut file)
        .unwrap();
    write!(&mut file, ";\n").unwrap();

    write!(&mut file, "const ALLOW_NON_CONSUMED_KEYS: phf::Set<&'static str> = ").unwrap();
    phf_codegen::Set::new()
        .entry("definitions")
        .entry("$schema")
        .entry("id")
        .entry("default")
        .entry("description")
        .entry("format")
        .build(&mut file)
        .unwrap();
    write!(&mut file, ";\n").unwrap();
}
