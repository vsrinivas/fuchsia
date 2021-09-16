// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::args::AutoTestGeneratorCommand;
use crate::cm_parser::read_cm;
use crate::generate_build::{CppBuildGenerator, RustBuildGenerator};
use crate::generate_cpp_test::{CppTestCode, CppTestCodeGenerator};
use crate::generate_manifest::{CppManifestGenerator, RustManifestGenerator};
use crate::generate_rust_test::{RustTestCode, RustTestCodeGenerator};
use crate::test_code::{CodeGenerator, TestCodeBuilder};

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
mod generate_cpp_test;
mod generate_manifest;
mod generate_rust_test;
mod test_code;

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
    let component_url: &str = &input.component_url.clone().unwrap_or(format!(
        "fuchsia-pkg://fuchsia.com/{}#meta/{}.cm",
        component_name, component_name
    ));

    // test_program_name defaults to {component_name}_test, this name will be used to generate
    // source code filename, and build rules.
    // ex: if component_name = echo_server
    //   rust code => echo_server_test.rs
    //   manifest  => meta/echo_server_test.cml
    let test_program_name = &format!("{}_test", &component_name);

    if input.cpp {
        write_cpp(&cm_decl, component_name, component_url, &test_program_name, &input)?;
    } else {
        write_rust(&cm_decl, component_name, component_url, &test_program_name, &input)?;
    }

    Ok(())
}

fn write_cpp(
    cm_decl: &ComponentDecl,
    component_name: &str,
    component_url: &str,
    output_file_name: &str,
    input: &AutoTestGeneratorCommand,
) -> Result<()> {
    let code = &mut CppTestCode::new(&component_name);

    // This tells RealmBuilder to wire-up the component-under-test.
    code.add_component(component_name, component_url, "COMPONENT_URL", false);

    // Add imports that all tests will need. For importing fidl libraries that we mock,
    // imports will be added when we call 'update_code_for_use_declaration'.
    code.add_import("<lib/sys/cpp/testing/realm_builder.h>");
    code.add_import("<lib/gtest/real_loop_fixture.h>");
    code.add_import("<gtest/gtest.h>");

    update_code_for_use_declaration(
        &cm_decl.uses.as_ref().unwrap_or(&Vec::new()),
        code,
        input.generate_mocks,
        true, /* cpp */
    )?;
    update_code_for_expose_declaration(
        &cm_decl.exposes.as_ref().unwrap_or(&Vec::new()),
        code,
        true, /* cpp */
    )?;

    let mut stdout = std::io::stdout();

    // Write cpp test code file
    let mut src_code_dest = PathBuf::from(&input.out_dir);
    src_code_dest.push("src");
    std::fs::create_dir_all(&src_code_dest).unwrap();
    src_code_dest.push(&output_file_name);
    src_code_dest.set_extension("cc");
    println!("writing cpp file to {}", src_code_dest.display());
    let cpp_code_generator = CppTestCodeGenerator { code };
    cpp_code_generator.write_file(&mut File::create(src_code_dest)?)?;
    if input.verbose {
        cpp_code_generator.write_file(&mut stdout)?;
    }

    let mut manifest_dest = PathBuf::from(&input.out_dir);
    manifest_dest.push("meta");
    std::fs::create_dir_all(&manifest_dest).unwrap();
    manifest_dest.push(&output_file_name);
    manifest_dest.set_extension("cml");
    println!("writing manifest file to {}", manifest_dest.display());
    let manifest_generator =
        CppManifestGenerator { test_program_name: output_file_name.to_string() };
    manifest_generator.write_file(&mut File::create(manifest_dest)?)?;
    if input.verbose {
        manifest_generator.write_file(&mut stdout)?;
    }

    // Write build file
    let mut build_dest = PathBuf::from(&input.out_dir);
    build_dest.push("BUILD.gn");
    println!("writing build file to {}", build_dest.display());
    let build_generator = CppBuildGenerator {
        test_program_name: output_file_name.to_string(),
        component_name: component_name.to_string(),
    };
    build_generator.write_file(&mut File::create(build_dest)?)?;
    if input.verbose {
        build_generator.write_file(&mut stdout)?;
    }

    Ok(())
}

fn write_rust(
    cm_decl: &ComponentDecl,
    component_name: &str,
    component_url: &str,
    output_file_name: &str,
    input: &AutoTestGeneratorCommand,
) -> Result<()> {
    let code = &mut RustTestCode::new(&component_name);

    code.add_component(component_name, component_url, "COMPONENT_URL", false);

    match input.generate_mocks {
        true => code.add_import("fuchsia_component_test::{builder::*, mock::*, RealmInstance}"),
        false => code.add_import("fuchsia_component_test::{builder::*,  RealmInstance}"),
    };

    update_code_for_use_declaration(
        &cm_decl.uses.as_ref().unwrap_or(&Vec::new()),
        code,
        input.generate_mocks,
        false, /* cpp */
    )?;
    update_code_for_expose_declaration(
        &cm_decl.exposes.as_ref().unwrap_or(&Vec::new()),
        code,
        false, /* cpp */
    )?;

    let mut stdout = std::io::stdout();

    // Write rust test code file
    let mut src_code_dest = PathBuf::from(&input.out_dir);
    src_code_dest.push("src");
    std::fs::create_dir_all(&src_code_dest).unwrap();
    src_code_dest.push(&output_file_name);
    src_code_dest.set_extension("rs");
    println!("writing rust file to {}", src_code_dest.display());
    let rust_code_generator = RustTestCodeGenerator { code };
    rust_code_generator.write_file(&mut File::create(src_code_dest)?)?;
    if input.verbose {
        rust_code_generator.write_file(&mut stdout)?;
    }

    // Write manifest cml file
    let mut manifest_dest = PathBuf::from(&input.out_dir);
    manifest_dest.push("meta");
    std::fs::create_dir_all(&manifest_dest).unwrap();
    manifest_dest.push(&output_file_name);
    manifest_dest.set_extension("cml");
    println!("writing manifest file to {}", manifest_dest.display());
    let manifest_generator =
        RustManifestGenerator { test_program_name: output_file_name.to_string() };
    manifest_generator.write_file(&mut File::create(manifest_dest)?)?;
    if input.verbose {
        manifest_generator.write_file(&mut stdout)?;
    }

    // Write build file
    let mut build_dest = PathBuf::from(&input.out_dir);
    build_dest.push("BUILD.gn");
    println!("writing build file to {}", build_dest.display());
    let build_generator = RustBuildGenerator {
        test_program_name: output_file_name.to_string(),
        component_name: component_name.to_string(),
    };
    build_generator.write_file(&mut File::create(build_dest)?)?;
    if input.verbose {
        build_generator.write_file(&mut stdout)?;
    }

    Ok(())
}

// Update TestCodeBuilder based on 'use' declarations in the .cm file
fn update_code_for_use_declaration(
    uses: &Vec<UseDecl>,
    code: &mut dyn TestCodeBuilder,
    gen_mocks: bool,
    cpp: bool,
) -> Result<()> {
    let mut dep_counter = 1;
    let mut dep_protocols = Protocols { protocols: HashMap::new() };

    for i in 0..uses.len() {
        match &uses[i] {
            UseDecl::Protocol(decl) => {
                if let Some(protocol) = &decl.source_name {
                    if TEST_REALM_CAPABILITIES.into_iter().any(|v| v == &protocol) {
                        code.add_protocol(protocol, "root", vec!["self".to_string()]);
                    } else {
                        let component_name = format!("service_{}", dep_counter);
                        // Note: we don't know which component offers this service, we'll
                        // label the service with a generic name: 'service_x', where x is
                        // a number. We'll use the generic name "{URL}" indicating that user
                        // needs to fill this value themselves.
                        if cpp {
                            dep_protocols.add_protocol_cpp(&protocol, &component_name)?;
                        } else {
                            dep_protocols.add_protocol_rust(&protocol, &component_name)?;
                        }

                        let component_url = "{URL}";
                        dep_counter += 1;
                        code.add_component(
                            &component_name,
                            &component_url,
                            &component_name.to_ascii_uppercase(),
                            gen_mocks,
                        );
                        // Note: "root" => test framework (i.e "RouteEndpoint::above_root()")
                        // "self" => component-under-test
                        code.add_protocol(
                            protocol,
                            &component_name,
                            vec!["root".to_string(), "self".to_string()],
                        );
                    }
                }
            }
            // Note: example outputs from parsing cm: http://go/paste/5523376119480320?raw
            UseDecl::Directory(decl) => {
                code.add_directory(
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
                code.add_storage(
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
    if gen_mocks && dep_protocols.protocols.len() > 0 {
        if cpp {
            code.add_import("<zircon/status.h>");
        } else {
            code.add_import("fuchsia_component::server::*");
        }
        for (component, protocols) in dep_protocols.protocols.iter() {
            for (fidl_lib, markers) in protocols.iter() {
                if cpp {
                    code.add_import(&format!("<{}>", fidl_lib));
                    // TODO(yuanzhi) What does the mock look like when we have multiple protocols to mock
                    // within the same component?
                    code.add_mock_impl(&component, &markers[0]);
                } else {
                    code.add_import(&format!("{}::*", fidl_lib));
                    // TODO(yuanzhi) Currently we ignore the 'protocol' field. But we can improve the generated
                    // mock function by auto generate function signature for the protocol that needs mocking.
                    code.add_mock_impl(&component, "");
                }
            }
        }
    }
    Ok(())
}

// Update TestCodeBuilder based on 'expose' declarations in the .cm file
fn update_code_for_expose_declaration(
    exposes: &Vec<ExposeDecl>,
    code: &mut dyn TestCodeBuilder,
    cpp: bool,
) -> Result<()> {
    let mut protos_to_test = Protocols { protocols: HashMap::new() };

    for i in 0..exposes.len() {
        match &exposes[i] {
            ExposeDecl::Protocol(decl) => {
                if let Some(protocol) = &decl.source_name {
                    code.add_protocol(protocol, "self", vec!["root".to_string()]);
                    if cpp {
                        protos_to_test.add_protocol_cpp(&protocol, "self")?;
                    } else {
                        protos_to_test.add_protocol_rust(&protocol, "self")?;
                    }
                }
            }
            _ => (),
        }
    }

    // Generate test case code for each protocol exposed
    for (fidl_lib, markers) in protos_to_test.protocols.get("self").unwrap().iter() {
        if cpp {
            code.add_import(&format!("<{}>", fidl_lib));
        } else {
            code.add_import(&format!("{}::*", fidl_lib));
        }
        for i in 0..markers.len() {
            code.add_test_case(&markers[i]);
        }
    }
    Ok(())
}

// Keeps track of all the protocol exposed or used by the component-under-test.
#[derive(Clone)]
struct Protocols {
    // This is a nested map
    // Outer map is keyed by component names, value is a map of fidl library and corresponding protocols.
    // Inner map is keyed by fidl library named, value is a list of protocols defined in the fidl_library.
    protocols: HashMap<String, HashMap<String, Vec<String>>>,
}

impl Protocols {
    pub fn add_protocol_rust<'a>(
        &'a mut self,
        protocol: &str,
        component: &str,
    ) -> Result<(), anyhow::Error> {
        let fields: Vec<&str> = protocol.split(".").collect();
        let mut fidl_lib = "fidl".to_string(); // ex: fidl_fuchsia_metrics
        for i in 0..(fields.len() - 1) {
            fidl_lib.push_str(format!("_{}", fields[i]).as_str());
        }
        let capture =
            Regex::new(r"^(?P<protocol>\w+)").unwrap().captures(fields.last().unwrap()).unwrap();

        let marker = self
            .protocols
            .entry(component.to_string())
            .or_insert_with_key(|_| HashMap::new())
            .entry(fidl_lib)
            .or_insert(Vec::new());
        marker.push(format!("{}Marker", capture.name("protocol").unwrap().as_str()));
        marker.dedup();
        Ok(())
    }

    pub fn add_protocol_cpp<'a>(
        &'a mut self,
        protocol: &str,
        component: &str,
    ) -> Result<(), anyhow::Error> {
        let fields: Vec<&str> = protocol.split(".").collect();
        let mut fidl_lib = "".to_string();
        let mut protocol_type = "".to_string(); // ex: fuchsia::metrics::MetricEventLogger
        for i in 0..(fields.len() - 1) {
            fidl_lib.push_str(format!("/{}", fields[i]).as_str());
            protocol_type.push_str(format!("{}::", fields[i]).as_str());
        }
        fidl_lib.push_str("/cpp/fidl.h"); // ex: /fuchsia/metrics/cpp/fidl.h

        let capture =
            Regex::new(r"^(?P<protocol>\w+)").unwrap().captures(fields.last().unwrap()).unwrap();

        let marker = self
            .protocols
            .entry(component.to_string())
            .or_insert_with_key(|_| HashMap::new())
            .entry(fidl_lib)
            .or_insert(Vec::new());
        marker.push(format!("{}{}", protocol_type, capture.name("protocol").unwrap().as_str()));
        marker.dedup();
        Ok(())
    }
}

#[cfg(test)]
mod test {
    use super::*;

    #[test]
    fn test_add_protocol_rust() -> Result<()> {
        let mut p = Protocols { protocols: HashMap::new() };
        p.add_protocol_rust("fuchsia.diagnostics.internal.FooController", "diagnostics")?;
        p.add_protocol_rust("fuchsia.diagnostics.internal.BarController-A", "diagnostics")?;
        p.add_protocol_rust("fuchsia.diagnostics.internal.BarController-B", "diagnostics")?;
        p.add_protocol_rust("fuchsia.metrics.FooController", "cobalt")?;
        p.add_protocol_rust("fuchsia.metrics.BarController", "cobalt")?;
        p.add_protocol_rust("fuchsia.metrics2.BazController", "cobalt")?;

        assert_eq!(p.protocols.len(), 2);
        assert!(p.protocols.contains_key("diagnostics"));
        assert!(p.protocols.contains_key("cobalt"));

        let d = p.protocols.get("diagnostics").unwrap();
        assert_eq!(d.len(), 1);
        assert!(d.contains_key("fidl_fuchsia_diagnostics_internal"));

        let d_markers = d.get("fidl_fuchsia_diagnostics_internal").unwrap();
        assert_eq!(d_markers.len(), 2);
        assert_eq!(d_markers[0], "FooControllerMarker");
        assert_eq!(d_markers[1], "BarControllerMarker");

        let c = p.protocols.get("cobalt").unwrap();
        assert_eq!(c.len(), 2);
        assert!(c.contains_key("fidl_fuchsia_metrics"));
        assert!(c.contains_key("fidl_fuchsia_metrics2"));

        let c_markers = c.get("fidl_fuchsia_metrics").unwrap();
        assert_eq!(c_markers.len(), 2);
        assert_eq!(c_markers[0], "FooControllerMarker");
        assert_eq!(c_markers[1], "BarControllerMarker");

        Ok(())
    }

    #[test]
    fn test_add_protocol_cpp() -> Result<()> {
        let mut p = Protocols { protocols: HashMap::new() };
        p.add_protocol_cpp("fuchsia.diagnostics.internal.FooController", "diagnostics")?;
        p.add_protocol_cpp("fuchsia.diagnostics.internal.BarController-A", "diagnostics")?;
        p.add_protocol_cpp("fuchsia.diagnostics.internal.BarController-B", "diagnostics")?;
        p.add_protocol_cpp("fuchsia.metrics.FooController", "cobalt")?;
        p.add_protocol_cpp("fuchsia.metrics.BarController", "cobalt")?;
        p.add_protocol_cpp("fuchsia.metrics2.BazController", "cobalt")?;

        assert_eq!(p.protocols.len(), 2);
        assert!(p.protocols.contains_key("diagnostics"));
        assert!(p.protocols.contains_key("cobalt"));

        let d = p.protocols.get("diagnostics").unwrap();
        assert_eq!(d.len(), 1);
        assert!(d.contains_key("/fuchsia/diagnostics/internal/cpp/fidl.h"));

        let d_markers = d.get("/fuchsia/diagnostics/internal/cpp/fidl.h").unwrap();
        assert_eq!(d_markers.len(), 2);
        assert_eq!(d_markers[0], "fuchsia::diagnostics::internal::FooController");
        assert_eq!(d_markers[1], "fuchsia::diagnostics::internal::BarController");

        let c = p.protocols.get("cobalt").unwrap();
        assert_eq!(c.len(), 2);
        assert!(c.contains_key("/fuchsia/metrics/cpp/fidl.h"));
        assert!(c.contains_key("/fuchsia/metrics2/cpp/fidl.h"));

        let c_markers = c.get("/fuchsia/metrics/cpp/fidl.h").unwrap();
        assert_eq!(c_markers.len(), 2);
        assert_eq!(c_markers[0], "fuchsia::metrics::FooController");
        assert_eq!(c_markers[1], "fuchsia::metrics::BarController");

        Ok(())
    }

    #[test]
    fn test_cpp_update_code_for_use_declaration() -> Result<()> {
        let use_protocol_1 = UseDecl::Protocol(UseProtocolDecl {
            source_name: Some("fuchsia.diagnostics.ArchiveAccessor".to_string()),
            ..UseProtocolDecl::EMPTY
        });
        let use_protocol_2 = UseDecl::Protocol(UseProtocolDecl {
            source_name: Some("fuchsia.metrics.MetricEventLoggerFactory".to_string()),
            ..UseProtocolDecl::EMPTY
        });
        let use_dir = UseDecl::Directory(UseDirectoryDecl {
            source_name: Some("config-data".to_string()),
            target_path: Some("/config/data".to_string()),
            ..UseDirectoryDecl::EMPTY
        });
        let component_name = "foo_bar";
        let uses = vec![use_protocol_1, use_protocol_2, use_dir];
        let code = &mut CppTestCode::new(&component_name);
        update_code_for_use_declaration(
            &uses, code, true, /* gen_mocks*/
            true, /* cpp */
        )?;
        let create_realm_impl = code.realm_builder_snippets.join("\n");
        let expect_realm_snippets = r#"      .AddComponent(
        sys::testing::Moniker{"service_1"},
        sys::testing::Component{.source = sys::testing::Mock {&mock_service_1}})
      .AddRoute(sys::testing::CapabilityRoute {
        .capability = sys::testing::Protocol {"fuchsia.diagnostics.ArchiveAccessor"},
        .source = sys::testing::Moniker{"service_1"},
        .targets = {sys::testing::AboveRoot(), sys::testing::Moniker{"foo_bar"}, }})
      .AddComponent(
        sys::testing::Moniker{"service_2"},
        sys::testing::Component{.source = sys::testing::Mock {&mock_service_2}})
      .AddRoute(sys::testing::CapabilityRoute {
        .capability = sys::testing::Protocol {"fuchsia.metrics.MetricEventLoggerFactory"},
        .source = sys::testing::Moniker{"service_2"},
        .targets = {sys::testing::AboveRoot(), sys::testing::Moniker{"foo_bar"}, }})
      .AddRoute(sys::testing::CapabilityRoute {
        .capability = sys::testing::Directory {
          .name = "config-data",
          .path = "/config/data",
          .rights = fuchsia::io2::RW_STAR_DIR,},
        .source = sys::testing::AboveRoot(),
        .targets = {sys::testing::Moniker{"foo_bar"}, }})"#;
        assert_eq!(create_realm_impl, expect_realm_snippets);

        let mut all_imports = code.imports.clone();
        all_imports.sort();
        let imports = all_imports.join("\n");
        let expect_imports = r#"#include </fuchsia/diagnostics/cpp/fidl.h>;
#include </fuchsia/metrics/cpp/fidl.h>;
#include <zircon/status.h>;"#;
        assert_eq!(imports, expect_imports);

        Ok(())
    }

    #[test]
    fn test_rust_update_code_for_use_declaration() -> Result<()> {
        let use_protocol_1 = UseDecl::Protocol(UseProtocolDecl {
            source_name: Some("fuchsia.diagnostics.ArchiveAccessor".to_string()),
            ..UseProtocolDecl::EMPTY
        });
        let use_protocol_2 = UseDecl::Protocol(UseProtocolDecl {
            source_name: Some("fuchsia.metrics.MetricEventLoggerFactory".to_string()),
            ..UseProtocolDecl::EMPTY
        });
        let use_dir = UseDecl::Directory(UseDirectoryDecl {
            source_name: Some("config-data".to_string()),
            target_path: Some("/config/data".to_string()),
            ..UseDirectoryDecl::EMPTY
        });
        let component_name = "foo_bar";
        let uses = vec![use_protocol_1, use_protocol_2, use_dir];
        let code = &mut RustTestCode::new(&component_name);
        update_code_for_use_declaration(
            &uses, code, true,  /* gen_mocks*/
            false, /* cpp */
        )?;
        let create_realm_impl = code.realm_builder_snippets.join("\n");
        let expect_realm_snippets = r#"        .add_component("service_1",
                ComponentSource::Mock(Mock::new(move |mock_handles: MockHandles| {
                    Box::pin(service_1_impl(mock_handles))
                })),
            )
            .await?
        .add_route(CapabilityRoute {
            capability: Capability::protocol("fuchsia.diagnostics.ArchiveAccessor"),
            source: RouteEndpoint::component("service_1"),
            targets: vec![
                RouteEndpoint::above_root(), RouteEndpoint::component("foo_bar"),
            ],
        })?
        .add_component("service_2",
                ComponentSource::Mock(Mock::new(move |mock_handles: MockHandles| {
                    Box::pin(service_2_impl(mock_handles))
                })),
            )
            .await?
        .add_route(CapabilityRoute {
            capability: Capability::protocol("fuchsia.metrics.MetricEventLoggerFactory"),
            source: RouteEndpoint::component("service_2"),
            targets: vec![
                RouteEndpoint::above_root(), RouteEndpoint::component("foo_bar"),
            ],
        })?
        .add_route(CapabilityRoute {
            capability: Capability::directory(
                "config-data",
                "/config/data",
                fio2::RW_STAR_DIR),
            source: RouteEndpoint::above_root(),
            targets: vec![
                RouteEndpoint::component("foo_bar"),
            ],
        })?"#;
        assert_eq!(create_realm_impl, expect_realm_snippets);

        let mut all_imports = code.imports.clone();
        all_imports.sort();
        let imports = all_imports.join("\n");
        let expect_imports = r#"use fidl_fuchsia_diagnostics::*;
use fidl_fuchsia_metrics::*;
use fuchsia_component::server::*;"#;
        assert_eq!(imports, expect_imports);

        Ok(())
    }
}
