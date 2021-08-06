// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::Result;
use std::io::Write;

pub trait CodeGenerator {
    fn write_file<W: Write>(&self, writer: &mut W) -> Result<()>;
}

pub struct RustTestCodeGenerator {
    pub code: RustTestCode,
}

impl CodeGenerator for RustTestCodeGenerator {
    fn write_file<W: Write>(&self, writer: &mut W) -> Result<()> {
        let create_realm_func_start = r#"pub async fn create_realm() -> Result<RealmInstance, Error> {
    let mut builder = RealmBuilder::new().await?;
    builder
"#;
        let create_realm_func_end = r#"
    let instance = builder.build().create().await?;
    Ok(instance)
}

"#;
        // Add import statements
        let mut imports = self.code.imports.join("\n");
        imports.push_str("\n\n");
        writer.write_all(&imports.as_bytes())?;

        // Add constants, these are components urls
        let mut constants = self.code.constants.join("\n");
        constants.push_str("\n\n");
        writer.write_all(&constants.as_bytes())?;

        // Generate create_realm() function
        writer.write_all(&create_realm_func_start.as_bytes())?;

        let mut realm = self.code.realm_builder_snippets.join("\n");
        realm.push_str(";\n\n");
        writer.write_all(&realm.as_bytes())?;

        writer.write_all(&create_realm_func_end.as_bytes())?;

        // Add testcases, one per protocol
        let mut test_cases = self.code.test_case.join("\n\n");
        test_cases.push_str("\n");
        writer.write_all(&test_cases.as_bytes())?;

        Ok(())
    }
}

pub struct RustTestCode {
    /// library import strings
    imports: Vec<String>,
    /// constant strings
    constants: Vec<String>,
    /// RealmBuilder compatibility routing code
    realm_builder_snippets: Vec<String>,
    /// testcase functions
    test_case: Vec<String>,
    /// name used by RealmBuilder for the component-under-test
    component_under_test: String,
}

impl RustTestCode {
    pub fn new(component_name: &str) -> Self {
        RustTestCode {
            realm_builder_snippets: Vec::new(),
            constants: Vec::new(),
            imports: Vec::new(),
            test_case: Vec::new(),
            component_under_test: component_name.to_string(),
        }
    }
    pub fn add_import<'a>(&'a mut self, import_library: &str) -> &'a mut Self {
        self.imports.push(format!(r#"use {};"#, import_library));
        self
    }

    pub fn add_component<'a>(
        &'a mut self,
        component_name: &str,
        url: &str,
        const_var: &str,
    ) -> &'a mut Self {
        self.realm_builder_snippets.push(format!(
            r#"        .add_component("{}", ComponentSource::url({})).await?"#,
            component_name, const_var
        ));
        self.constants.push(format!(r#"const {}: &str = "{}";"#, const_var, url).to_string());
        self
    }

    pub fn add_protocol<'a>(
        &'a mut self,
        protocol: &str,
        source: &str,
        targets: Vec<String>,
    ) -> &'a mut Self {
        let source_code = match source {
            "root" => "RouteEndpoint::above_root()".to_string(),
            "self" => format!("RouteEndpoint::component(\"{}\"), ", self.component_under_test),
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

    pub fn add_directory<'a>(
        &'a mut self,
        dir_name: &str,
        dir_path: &str,
        targets: Vec<String>,
    ) -> &'a mut Self {
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

    pub fn add_storage<'a>(
        &'a mut self,
        storage_name: &str,
        storage_path: &str,
        targets: Vec<String>,
    ) -> &'a mut Self {
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

    pub fn add_test_case<'a>(&'a mut self, marker: &str) -> &'a mut Self {
        self.test_case.push(format!(
            r#"#[fuchsia::test]
async fn test_{}() -> Result<(), Error> {{
    let instance = create_realm().await.expect("created testing realm");
    let {} = instance.root.connect_to_protocol_at_exposed_dir::<{}>()?;
}}"#,
            marker.to_ascii_lowercase(),
            marker.to_ascii_lowercase(),
            marker
        ));
        self
    }
}
