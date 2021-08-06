// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::args::AutoTestGeneratorCommand;
use crate::cm_parser::read_cm;
use crate::generate_build::BuildGenerator;
use crate::generate_manifest::ManifestGenerator;
use crate::generate_rust_test::{CodeGenerator, RustTestCode, RustTestCodeGenerator};

use anyhow::{format_err, Result};
use fidl_fuchsia_sys2::*;
use regex::Regex;
use std::collections::HashMap;
use std::fs::File;
use std::path::Path;
use std::path::PathBuf;
mod args;
mod cm_parser;
mod generate_build;
mod generate_manifest;
mod generate_rust_test;

// protocols provided by component test framework, if the component-under-test 'use' any of
// these protocols, the capability source will be routed from RouteEndpoint::above_root()
const TEST_REALM_CAPABILITIES: &'static [&'static str] = &["fuchsia.logger.LogSink"];

fn main() -> Result<()> {
    let input: AutoTestGeneratorCommand = argh::from_env();

    // component_name is the filename from the full path passed to the --cm-location.
    // ex: for '--cm-location src/diagnostics/log-stats/cml/component/log-stats.cm',
    // component_name will be log-stats
    let component_name: &str = Path::new(&input.cm_location)
        .file_stem()
        .ok_or_else(|| format_err!("{} is missing a file name", &input.cm_location))?
        .to_str()
        .ok_or_else(|| format_err!("{} cannot be converted to utf-8", &input.cm_location))?;

    let cm_decl = read_cm(&input.cm_location)
        .map_err(|e| format_err!("parsing .cm file '{}' errored: {:?}", &input.cm_location, e))?;

    // component_url is either what user specified via --component-url or default to the format
    // 'fuchsia-pkg://fuchsia.com/{component_name}#meta/{component_neame}.cm'
    let component_url: &str = &input.component_url.unwrap_or(format!(
        "fuchsia-pkg://fuchsia.com/{}#meta/{}.cm",
        component_name, component_name
    ));

    // test_program_name defaults to {component_name}_test, this name will be used to generate
    // source code filename, and build rules.
    // ex: if component_name = echo_server
    //   rust code => echo_server_test.rs
    //   manifest  => meta/echo_server_test.cml
    let test_program_name = &format!("{}_test", &component_name);

    let mut rust_code = RustTestCode::new(&component_name);
    rust_code.add_component(component_name, component_url, "COMPONENT_URL");
    rust_code.add_import("fuchsia_component_test::{builder::*, mock::*, RealmInstance}");

    update_rust_code_for_use_declaration(&cm_decl.uses.unwrap_or(Vec::new()), &mut rust_code)?;
    update_rust_code_for_expose_declaration(
        &cm_decl.exposes.unwrap_or(Vec::new()),
        &mut rust_code,
    )?;

    let mut stdout = std::io::stdout();

    // Write rust test code file
    let mut src_code_dest = PathBuf::from(&input.out_dir);
    std::fs::create_dir_all(&src_code_dest).unwrap();
    src_code_dest.push(&test_program_name);
    src_code_dest.set_extension("rs");
    println!("writing rust file to {}", src_code_dest.display());
    let rust_code_generator = RustTestCodeGenerator { code: rust_code };
    rust_code_generator.write_file(&mut File::create(src_code_dest)?)?;
    if input.verbose {
        rust_code_generator.write_file(&mut stdout)?;
    }

    // Write manifest cml file
    let mut manifest_dest = PathBuf::from(&input.out_dir);
    manifest_dest.push("meta");
    std::fs::create_dir_all(&manifest_dest).unwrap();
    manifest_dest.push(&test_program_name);
    manifest_dest.set_extension("cml");
    println!("writing manifest file to {}", manifest_dest.display());
    let manifest_generator = ManifestGenerator { test_program_name: test_program_name.to_string() };
    manifest_generator.write_file(&mut File::create(manifest_dest)?)?;
    if input.verbose {
        manifest_generator.write_file(&mut stdout)?;
    }

    // Write build file
    let mut build_dest = PathBuf::from(&input.out_dir);
    build_dest.push("BUILD.gn");
    println!("writing build file to {}", build_dest.display());
    let build_generator = BuildGenerator {
        test_program_name: test_program_name.to_string(),
        component_name: component_name.to_string(),
    };
    build_generator.write_file(&mut File::create(build_dest)?)?;
    if input.verbose {
        build_generator.write_file(&mut stdout)?;
    }

    Ok(())
}

// Update RustTestCode based on 'use' declarations in the .cm file
fn update_rust_code_for_use_declaration(
    uses: &Vec<UseDecl>,
    rust_code: &mut RustTestCode,
) -> Result<()> {
    let mut dep_counter = 1;
    for i in 0..uses.len() {
        match &uses[i] {
            UseDecl::Protocol(decl) => {
                if let Some(protocol) = &decl.source_name {
                    if TEST_REALM_CAPABILITIES.into_iter().any(|v| v == &protocol) {
                        rust_code.add_protocol(protocol, "root", vec!["self".to_string()]);
                    } else {
                        // Note: we don't know which component offers this service, we'll
                        // label the service with a generic name: 'service_x', where x is
                        // a number. We'll use the generic name "{URL}" indicating that user
                        // needs to fill this value themselves.
                        //
                        // TODO(yuanzhi) currently we don't keep track of this service,
                        // figure out if we need to route any root services to this service.
                        //
                        // TODO(yuanzhi) support a way for user to specify to auto-gen
                        // mocks instead of launching the actual component.
                        let component_name = format!("service_{}", dep_counter);
                        let component_url = "{URL}";
                        dep_counter += 1;
                        rust_code.add_component(
                            &component_name,
                            &component_url,
                            &component_name.to_ascii_uppercase(),
                        );
                        // Note: "root" => test framework (i.e "RouteEndpoint::above_root()")
                        // "self" => component-under-test
                        rust_code.add_protocol(
                            protocol,
                            &component_name,
                            vec!["root".to_string(), "self".to_string()],
                        );
                    }
                }
            }
            // Note: example outputs from parsing cm: http://go/paste/5523376119480320?raw
            UseDecl::Directory(decl) => {
                rust_code.add_directory(
                    decl.source_name
                        .as_ref()
                        .ok_or(format_err!("directory name needs to be specified in manifest."))?,
                    decl.target_path
                        .as_ref()
                        .ok_or(format_err!("directory path needs to be specified in manifest."))?,
                    vec!["self".to_string()],
                );
            }
            UseDecl::Storage(decl) => {
                rust_code.add_storage(
                    decl.source_name
                        .as_ref()
                        .ok_or(format_err!("storage name needs to be specified in manifest."))?,
                    decl.target_path
                        .as_ref()
                        .ok_or(format_err!("storage path needs to be specified in manifest."))?,
                    vec!["self".to_string()],
                );
            }
            _ => (),
        }
    }
    Ok(())
}

// Update RustTestCode based on 'expose' declarations in the .cm file
fn update_rust_code_for_expose_declaration(
    exposes: &Vec<ExposeDecl>,
    rust_code: &mut RustTestCode,
) -> Result<()> {
    let mut protos_to_test = ProtocolsToTest { protocols_to_test: HashMap::new() };

    for i in 0..exposes.len() {
        match &exposes[i] {
            ExposeDecl::Protocol(decl) => {
                if let Some(protocol) = &decl.source_name {
                    rust_code.add_protocol(protocol, "self", vec!["root".to_string()]);
                    protos_to_test.add_protocol(&protocol)?;
                }
            }
            _ => (),
        }
    }

    // Generate test case code for each protocol exposed
    for (fidl_lib, markers) in protos_to_test.protocols_to_test.iter() {
        rust_code.add_import(&format!("{}::*", fidl_lib));
        for i in 0..markers.len() {
            rust_code.add_test_case(&markers[i]);
        }
    }
    Ok(())
}

// Keeps track of all the protocol exposed by the component-under-test.
#[derive(Clone)]
struct ProtocolsToTest {
    protocols_to_test: HashMap<String, Vec<String>>,
}

impl ProtocolsToTest {
    pub fn add_protocol<'a>(&'a mut self, protocol: &str) -> Result<(), anyhow::Error> {
        let fields: Vec<&str> = protocol.split(".").collect();
        let mut fidl_lib = "fidl".to_string();
        for i in 0..(fields.len() - 1) {
            fidl_lib.push_str(format!("_{}", fields[i]).as_str());
        }
        let capture =
            Regex::new(r"^(?P<protocol>\w+)").unwrap().captures(fields.last().unwrap()).unwrap();

        let marker = self.protocols_to_test.entry(fidl_lib).or_insert(Vec::new());
        marker.push(format!("{}Marker", capture.name("protocol").unwrap().as_str()));
        marker.dedup();
        Ok(())
    }
}

#[cfg(test)]
mod test {
    use super::*;
    #[test]
    fn test_add_protocol() -> Result<()> {
        let mut p = ProtocolsToTest { protocols_to_test: HashMap::new() };
        p.add_protocol("fuchsia.diagnostics.internal.FooController")?;
        p.add_protocol("fuchsia.diagnostics.internal.BarController-A")?;
        p.add_protocol("fuchsia.diagnostics.internal.BarController-B")?;
        assert!(p.protocols_to_test.contains_key("fidl_fuchsia_diagnostics_internal"));
        assert_eq!(p.protocols_to_test.len(), 1);

        let markers = p.protocols_to_test.get("fidl_fuchsia_diagnostics_internal").unwrap();
        assert_eq!(markers.len(), 2);
        assert_eq!(markers[0], "FooControllerMarker");
        assert_eq!(markers[1], "BarControllerMarker");

        Ok(())
    }
}
