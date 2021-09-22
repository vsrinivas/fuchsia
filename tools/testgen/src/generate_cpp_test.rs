// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::Result;
use std::io::Write;

use crate::test_code::{CodeGenerator, TestCodeBuilder};

const MOCK_FUNC_TEMPLATE: &'static str = include_str!("templates/template_cpp_mock_function");
const TEST_FUNC_TEMPLATE: &'static str = include_str!("templates/template_cpp_test_function");

pub struct CppTestCodeGenerator<'a> {
    pub code: &'a CppTestCode,
}

impl CodeGenerator for CppTestCodeGenerator<'_> {
    fn write_file<W: Write>(&self, writer: &mut W) -> Result<()> {
        let mut test_fixture = format!(
            r#"
class {}Test: public ::gtest::RealLoopFixture {{
"#,
            self.code.component_camel_name
        );

        if self.code.mock_functions.len() > 0 {
            test_fixture.push_str(" public:\n");
            for mock in &self.code.mock_functions {
                test_fixture.push_str(
                    format!("  std::unique_ptr<{}> {};\n", mock.class_name, mock.handle_name)
                        .as_str(),
                );
            }
        }
        test_fixture.push_str(
            "
 protected:
  void SetUp() override {
    context_ = sys::ComponentContext::Create();
  }

  sys::ComponentContext* context() { return context_.get(); }

  std::unique_ptr<sys::testing::Realm> CreateRealm() {
",
        );

        for mock in &self.code.mock_functions {
            // Adding code to create the mock handles in CreateRealm().
            test_fixture.push_str(
                format!(
                    "    {} = std::make_unique<{}>(dispatcher());\n",
                    mock.handle_name, mock.class_name
                )
                .as_str(),
            );
        }
        test_fixture.push_str(
            r#"
    auto realm_builder = sys::testing::Realm::Builder::New(context());
    realm_builder
"#,
        );

        test_fixture.push_str(&self.code.realm_builder_snippets.join("\n"));
        test_fixture.push_str(";\n\n");
        test_fixture.push_str(
            r#"    return std::make_unique<sys::testing::Realm>(realm_builder.Build(dispatcher()));
  }

 private:
  std::unique_ptr<sys::ComponentContext> context_;
};

"#,
        );

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

        // Generate Test Fixture class() function
        writer.write_all(&test_fixture.as_bytes())?;

        // Add mock implementation functions, one per component
        for mock in &self.code.mock_functions {
            writer.write_all(mock.function_impl.as_bytes())?;
            writer.write_all(b"\n")?;
        }

        // Add testcases, one per protocol
        let mut test_cases = self.code.test_case.join("\n\n");
        test_cases.push_str("\n");
        writer.write_all(&test_cases.as_bytes())?;

        Ok(())
    }
}

fn convert_to_camel(input: &str) -> String {
    let mut camel = "".to_string();
    // Always capitalize first char
    let mut capitalize = true;
    for c in input.chars() {
        if capitalize {
            camel.push(c.to_ascii_uppercase());
            capitalize = false;
            continue;
        }
        if c == '-' || c == '_' {
            capitalize = true;
            continue;
        }
        camel.push(c);
    }
    return camel;
}

pub struct MockService {
    class_name: String,
    handle_name: String,
    function_impl: String,
}
pub struct CppTestCode {
    /// library import strings
    pub imports: Vec<String>,
    /// constant strings
    constants: Vec<String>,
    /// RealmBuilder compatibility routing code
    pub realm_builder_snippets: Vec<String>,
    /// testcase functions
    test_case: Vec<String>,
    /// stores information on mock implementions
    mock_functions: Vec<MockService>,
    /// name used by RealmBuilder for the component-under-test
    component_under_test: String,
    /// CamelCase version of the component name used to name cpp class name
    component_camel_name: String,
}

impl TestCodeBuilder for CppTestCode {
    fn new(component_name: &str) -> Self {
        let component_camel_name = convert_to_camel(component_name);
        CppTestCode {
            realm_builder_snippets: Vec::new(),
            constants: Vec::new(),
            imports: Vec::new(),
            test_case: Vec::new(),
            mock_functions: Vec::new(),
            component_under_test: component_name.to_string(),
            component_camel_name: component_camel_name,
        }
    }
    fn add_import<'a>(&'a mut self, import_library: &str) -> &'a dyn TestCodeBuilder {
        self.imports.push(format!(r#"#include {};"#, import_library));
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
            let mock_handle_name = format!("mock_{}", component_name);
            self.realm_builder_snippets.push(format!(
                r#"      .AddComponent(
        sys::testing::Moniker{{"{}"}},
        sys::testing::Component{{.source = sys::testing::Mock {{&{}}}}})"#,
                component_name, &mock_handle_name,
            ));
        } else {
            self.realm_builder_snippets.push(format!(
                r#"      .AddComponent(
        sys::testing::Moniker{{"{}"}},
        sys::testing::Component{{.source = sys::testing::ComponentUrl {{{}}}}})"#,
                component_name, const_var
            ));
            self.constants
                .push(format!(r#"constexpr char {}[] = "{}";"#, const_var, url).to_string());
        }
        self
    }

    fn add_mock_impl<'a>(
        &'a mut self,
        component_name: &str,
        protocol: &str,
    ) -> &'a dyn TestCodeBuilder {
        let mock_class_name = format!("Mock{}", component_name);
        let mock_handle_name = format!("mock_{}", component_name);
        self.mock_functions.push(MockService {
            function_impl: MOCK_FUNC_TEMPLATE
                .replace("CLASS_NAME", &mock_class_name)
                .replace("COMPONENT_PROTOCOL_NAME", &protocol),
            class_name: mock_class_name,
            handle_name: mock_handle_name,
        });
        self
    }

    fn add_protocol<'a>(
        &'a mut self,
        protocol: &str,
        source: &str,
        targets: Vec<String>,
    ) -> &'a dyn TestCodeBuilder {
        let source_code = match source {
            "root" => "sys::testing::AboveRoot()".to_string(),
            "self" => format!("sys::testing::Moniker{{\"{}\"}}", self.component_under_test),
            _ => format!("sys::testing::Moniker{{\"{}\"}}", source),
        };

        let mut targets_code: String = "".to_string();
        for i in 0..targets.len() {
            let t = &targets[i];
            match t.as_str() {
                "root" => targets_code.push_str("sys::testing::AboveRoot(), "),
                "self" => targets_code.push_str(
                    format!("sys::testing::Moniker{{\"{}\"}}, ", self.component_under_test)
                        .as_str(),
                ),
                _ => {
                    targets_code.push_str(format!("sys::testing::Moniker{{\"{}\"}}, ", t).as_str())
                }
            }
        }
        self.realm_builder_snippets.push(format!(
            r#"      .AddRoute(sys::testing::CapabilityRoute {{
        .capability = sys::testing::Protocol {{"{}"}},
        .source = {},
        .targets = {{{}}}}})"#,
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
            match t.as_str() {
                "root" => targets_code.push_str("sys::testing::AboveRoot(), "),
                "self" => targets_code.push_str(
                    format!("sys::testing::Moniker{{\"{}\"}}, ", self.component_under_test)
                        .as_str(),
                ),
                _ => {
                    targets_code.push_str(format!("sys::testing::Moniker{{\"{}\"}}, ", t).as_str())
                }
            }
        }
        self.realm_builder_snippets.push(format!(
            r#"      .AddRoute(sys::testing::CapabilityRoute {{
        .capability = sys::testing::Directory {{
          .name = "{}",
          .path = "{}",
          .rights = fuchsia::io2::RW_STAR_DIR,}},
        .source = sys::testing::AboveRoot(),
        .targets = {{{}}}}})"#,
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
            match t.as_str() {
                "root" => targets_code.push_str("sys::testing::AboveRoot(), "),
                "self" => targets_code.push_str(
                    format!("sys::testing::Moniker{{\"{}\"}}, ", self.component_under_test)
                        .as_str(),
                ),
                _ => {
                    targets_code.push_str(format!("sys::testing::Moniker{{\"{}\"}}, ", t).as_str())
                }
            }
        }
        self.realm_builder_snippets.push(format!(
            r#"      .AddRoute(sys::testing::CapabilityRoute {{
        .capability = sys::testing::Storage {{
          .name = "{}",
          .path = "{}",}},
        .source = sys::testing::AboveRoot(),
        .targets = {{{}}}}})"#,
            storage_name, storage_path, targets_code
        ));
        self
    }

    fn add_test_case<'a>(&'a mut self, marker: &str) -> &'a dyn TestCodeBuilder {
        let fields: Vec<&str> = marker.rsplit("::").collect();
        self.test_case.push(
            TEST_FUNC_TEMPLATE
                .replace("TEST_CLASS_NAME", format!("{}Test", self.component_camel_name).as_str())
                .replace("TEST_NAME", fields[0])
                .replace("PROTOCOL_TYPE", &marker),
        );
        self
    }
}

#[cfg(test)]
mod test {
    use super::*;

    #[test]
    fn test_convert_camel_case() -> Result<()> {
        let camel_1 = convert_to_camel("log-stats");
        assert_eq!(camel_1, "LogStats");

        let camel_2 = convert_to_camel("log_stats");
        assert_eq!(camel_2, "LogStats");

        let camel_3 = convert_to_camel("LogStats");
        assert_eq!(camel_3, "LogStats");

        let camel_4 = convert_to_camel("logstats");
        assert_eq!(camel_4, "Logstats");

        Ok(())
    }
}
