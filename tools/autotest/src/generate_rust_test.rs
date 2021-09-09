// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::Result;
use std::io::Write;

use crate::test_code::{CodeGenerator, TestCodeBuilder};

const MOCK_FUNC_TEMPLATE: &'static str = include_str!("templates/template_rust_mock_function");
const TEST_FUNC_TEMPLATE: &'static str = include_str!("templates/template_rust_test_function");

pub struct RustTestCodeGenerator<'a> {
    pub code: &'a RustTestCode,
}

impl CodeGenerator for RustTestCodeGenerator<'_> {
    fn write_file<W: Write>(&self, writer: &mut W) -> Result<()> {
        let create_realm_func_start = r#"pub async fn create_realm() -> Result<RealmInstance, Error> {
    let mut builder = RealmBuilder::new().await?;
    builder
"#;
        let mut create_realm_impl = self.code.realm_builder_snippets.join("\n");
        create_realm_impl.push_str(";\n\n");
        let create_realm_func_end = r#"
    let instance = builder.build().create().await?;
    Ok(instance)
}

"#;
        // Add import statements
        let mut all_imports = self.code.imports.clone();
        all_imports.sort();
        let mut imports = all_imports.join("\n");
        imports.push_str("\n\n");
        writer.write_all(&imports.as_bytes())?;

        // Add constants, these are components urls
        let mut constants = self.code.constants.join("\n");
        constants.push_str("\n\n");
        writer.write_all(&constants.as_bytes())?;

        // Generate create_realm() function
        writer.write_all(&create_realm_func_start.as_bytes())?;
        writer.write_all(&create_realm_impl.as_bytes())?;
        writer.write_all(&create_realm_func_end.as_bytes())?;

        // Add mock implementation functions, one per component
        if self.code.mock_functions.len() > 0 {
            let mut mock_funcs = self.code.mock_functions.join("\n\n");
            mock_funcs.push_str("\n\n");
            writer.write_all(&mock_funcs.as_bytes())?;
        }

        // Add testcases, one per protocol
        let mut test_cases = self.code.test_case.join("\n\n");
        test_cases.push_str("\n");
        writer.write_all(&test_cases.as_bytes())?;

        Ok(())
    }
}

pub struct RustTestCode {
    /// library import strings
    pub imports: Vec<String>,
    /// constant strings
    constants: Vec<String>,
    /// RealmBuilder compatibility routing code
    pub realm_builder_snippets: Vec<String>,
    /// testcase functions
    test_case: Vec<String>,
    // skeleton functions for implementing mocks
    mock_functions: Vec<String>,
    /// name used by RealmBuilder for the component-under-test
    component_under_test: String,
}

impl TestCodeBuilder for RustTestCode {
    fn new(component_name: &str) -> Self {
        RustTestCode {
            realm_builder_snippets: Vec::new(),
            constants: Vec::new(),
            imports: Vec::new(),
            test_case: Vec::new(),
            mock_functions: Vec::new(),
            component_under_test: component_name.to_string(),
        }
    }
    fn add_import<'a>(&'a mut self, import_library: &str) -> &'a dyn TestCodeBuilder {
        self.imports.push(format!(r#"use {};"#, import_library));
        self
    }

    fn add_component<'a>(
        &'a mut self,
        component_name: &str,
        url: &str,
        const_var: &str,
        mock: bool,
    ) -> &'a dyn TestCodeBuilder {
        if mock {
            let mock_function_name = format!("{}_impl", component_name);
            self.realm_builder_snippets.push(format!(
                r#"        .add_component("{}",
                ComponentSource::Mock(Mock::new(move |mock_handles: MockHandles| {{
                    Box::pin({}(mock_handles))
                }})),
            )
            .await?"#,
                component_name, &mock_function_name,
            ));
        } else {
            self.realm_builder_snippets.push(format!(
                r#"        .add_component("{}", ComponentSource::url({})).await?"#,
                component_name, const_var
            ));
            self.constants.push(format!(r#"const {}: &str = "{}";"#, const_var, url).to_string());
        }
        self
    }

    fn add_mock_impl<'a>(
        &'a mut self,
        component_name: &str,
        _protocol: &str,
    ) -> &'a dyn TestCodeBuilder {
        // Note: this function name must match the one we added in 'add_component'.
        let mock_function_name = format!("{}_impl", component_name);
        self.mock_functions.push(MOCK_FUNC_TEMPLATE.replace("FUNCTION_NAME", &mock_function_name));
        self
    }

    fn add_protocol<'a>(
        &'a mut self,
        protocol: &str,
        source: &str,
        targets: Vec<String>,
    ) -> &'a dyn TestCodeBuilder {
        let source_code = match source {
            "root" => "RouteEndpoint::above_root()".to_string(),
            "self" => format!("RouteEndpoint::component(\"{}\")", self.component_under_test),
            _ => format!("RouteEndpoint::component(\"{}\")", source),
        };

        let mut targets_code: String = "".to_string();
        for i in 0..targets.len() {
            let t = &targets[i];
            if t == "root" {
                targets_code.push_str("RouteEndpoint::above_root(), ");
            } else if t == "self" {
                targets_code.push_str(
                    format!("RouteEndpoint::component(\"{}\"), ", self.component_under_test)
                        .as_str(),
                );
            } else {
                targets_code.push_str(format!("RouteEndpoint::component(\"{}\"), ", t).as_str());
            }
        }
        self.realm_builder_snippets.push(format!(
            r#"        .add_route(CapabilityRoute {{
            capability: Capability::protocol("{}"),
            source: {},
            targets: vec![
                {}
            ],
        }})?"#,
            protocol, source_code, targets_code
        ));
        self
    }

    fn add_directory<'a>(
        &'a mut self,
        dir_name: &str,
        dir_path: &str,
        targets: Vec<String>,
    ) -> &'a dyn TestCodeBuilder {
        let mut targets_code: String = "".to_string();
        for i in 0..targets.len() {
            let t = &targets[i];
            if t == "root" {
                targets_code.push_str("RouteEndpoint::above_root(), ");
            } else if t == "self" {
                targets_code.push_str(
                    format!("RouteEndpoint::component(\"{}\"), ", self.component_under_test)
                        .as_str(),
                );
            } else {
                targets_code.push_str(format!("RouteEndpoint::component(\"{}\"), ", t).as_str());
            }
        }
        self.realm_builder_snippets.push(format!(
            r#"        .add_route(CapabilityRoute {{
            capability: Capability::directory(
                "{}",
                "{}",
                fio2::RW_STAR_DIR),
            source: RouteEndpoint::above_root(),
            targets: vec![
                {}
            ],
        }})?"#,
            dir_name, dir_path, targets_code
        ));
        self
    }

    fn add_storage<'a>(
        &'a mut self,
        storage_name: &str,
        storage_path: &str,
        targets: Vec<String>,
    ) -> &'a dyn TestCodeBuilder {
        let mut targets_code: String = "".to_string();
        for i in 0..targets.len() {
            let t = &targets[i];
            if t == "root" {
                targets_code.push_str("RouteEndpoint::above_root(), ");
            } else if t == "self" {
                targets_code.push_str(
                    format!("RouteEndpoint::component(\"{}\"), ", self.component_under_test)
                        .as_str(),
                );
            } else {
                targets_code.push_str(format!("RouteEndpoint::component(\"{}\"), ", t).as_str());
            }
        }
        self.realm_builder_snippets.push(format!(
            r#"        .add_route(CapabilityRoute {{
            capability: Capability::storage(
                "{}",
                "{}",
            ),
            source: RouteEndpoint::above_root(),
            targets: vec![
                {}
            ],
        }})?"#,
            storage_name, storage_path, targets_code
        ));
        self
    }

    fn add_test_case<'a>(&'a mut self, marker: &str) -> &'a dyn TestCodeBuilder {
        self.test_case.push(
            TEST_FUNC_TEMPLATE
                .replace("MARKER_VAR_NAME", &marker.to_ascii_lowercase())
                .replace("MARKER", &marker),
        );
        self
    }
}
