// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::{Context as _, Error};
use handlebars::{Context, Handlebars, Helper, Output, RenderContext, RenderError};
use heck::SnakeCase;
use serde_json::{json, Value};
use std::fs::File;
use std::io::Write;
use std::path::PathBuf;
use tracing::info;

use crate::templates::{FidldocTemplate, HandlebarsHelper};

// Handlebars "handler" to extract a docstring for a given FIDL JSON item.
fn docstring(
    h: &Helper<'_, '_>,
    _: &Handlebars<'_>,
    _: &Context,
    _: &mut RenderContext<'_, '_>,
    out: &mut dyn Output,
) -> Result<(), RenderError> {
    let param =
        h.param(0).ok_or_else(|| RenderError::new("Param 0 is required for docstring helper"))?;
    out.write(&extract_item_docstring(&param.value()))?;
    Ok(())
}

// Some FIDL constructs use arrays of attributes, identified with a "name" key. This returns the
// element of the given array with the given name, or Null if not found.
fn array_elt_with_name<'a>(value: &'a Value, name: &str) -> Option<&'a Value> {
    match value {
        Value::Array(array) => array.iter().find(|elt| elt["name"] == name),
        _ => None,
    }
}

// Returns the given named attribute or None for the given item.
fn attribute_with_name<'a>(value: &'a Value, name: &str) -> Option<&'a Value> {
    array_elt_with_name(&value["maybe_attributes"], name)
}

// FIDL docstrings are formatted like "/// foo" and the value of the docstring in the JSON is just
// this text with the "///" trimmed. The result is that each line has a leading space. This function
// removes the leading spaces from each line.
fn replace_docstring_leading_spaces(input: &String) -> String {
    let bytes = input.as_bytes();
    let mut result = Vec::new();

    let mut i = 0usize;
    while i < bytes.len() {
        if i == 0 && bytes[i] == ' ' as u8 {
            // Initial space at beginning of string.
            i += 1;
        } else if bytes[i] == '\n' as u8 && i < bytes.len() - 1 && bytes[i + 1] == ' ' as u8 {
            // Newline followed by space.
            result.push(bytes[i]);
            i += 2;
        } else {
            // Normal character.
            result.push(bytes[i]);
            i += 1;
        }
    }

    String::from_utf8(result).unwrap()
}

// We want to extract the JSON path "maybe_attributes[X].arguments[0].value.value"
// where [X] is the index of the array element with attribute name="doc".
fn extract_item_docstring(protocol: &Value) -> String {
    if let Some(doc) = attribute_with_name(&protocol, "doc") {
        if let Value::Array(args) = &doc["arguments"] {
            if args.len() == 1 {
                if let Some(s) = args[0]["value"]["value"].as_str() {
                    return replace_docstring_leading_spaces(&s.to_string());
                }
            }
        }
    }

    // Fall back to the protocol name when there is no docstring.
    if let Some(s) = protocol["name"].as_str() {
        // Trim a leading package name ("zx/").
        if let Some(slash_idx) = s.find("/") {
            s[slash_idx + 1..].trim().to_string()
        } else {
            s.trim().to_string()
        }
    } else {
        String::new()
    }
}

pub struct SyscallTemplate<'a> {
    handlebars: Handlebars<'a>,
    output_path: PathBuf,
}

impl<'a> SyscallTemplate<'a> {
    pub fn new(output_path: &PathBuf) -> SyscallTemplate<'a> {
        // Handlebars
        let mut handlebars = Handlebars::new();

        // Register core templates
        for &(name, template, expect) in
            &[("syscall", include_str!("syscall.hbs"), "Failed to include syscall")]
        {
            handlebars.register_template_string(name, template).expect(expect);
        }

        // Register helpers
        let helpers: &[(&str, HandlebarsHelper)] = &[
            ("getLink", crate::templates::get_link_helper),
            ("rpn", crate::templates::remove_package_name),
            ("eq", crate::templates::eq),
            ("len", crate::templates::len),
            ("pl", crate::templates::package_link),
            ("rpf", crate::templates::remove_parent_folders),
            ("sl", crate::templates::source_link),
            ("docLink", crate::templates::doc_link),
            ("oneline", crate::templates::one_line),
            ("docstring", docstring),
            ("lower_snake_case", crate::templates::lower_snake_case),
            ("pulldown", crate::templates::pulldown),
            ("methodId", crate::templates::method_id),
            ("processVersions", crate::templates::process_versions),
        ];
        for &(name, helper) in helpers {
            handlebars.register_helper(name, Box::new(helper));
        }

        SyscallTemplate { handlebars: handlebars, output_path: output_path.to_path_buf() }
    }

    fn render_syscall(
        &self,
        root_json: &Value,
        family_name: &String,
        syscall_json: &Value,
    ) -> Result<(), Error> {
        let method_name =
            syscall_json["name"].as_str().ok_or_else(|| RenderError::new("Invalid name"))?;

        // If `family_name` is set, then the syscall name is
        // `zx_${family_name}_${method_name}`; else it is `zx_${method_name}`.
        let mut syscall_name = String::new();
        if !family_name.is_empty() {
            syscall_name.push_str(family_name);
            syscall_name.push_str("_");
        }
        syscall_name.push_str(method_name);
        syscall_name = syscall_name.as_str().to_snake_case().to_lowercase();

        info!("Rendering {}", syscall_name);
        let output_path = self.output_path.join(syscall_name.clone() + &".md".to_string());
        let mut output_file = File::create(&output_path)
            .with_context(|| format!("Can't create file {}", output_path.display()))?;

        // Construct the JSON context to send to the template. It contains the global config and the
        // current syscall to render.
        let template_json = json!({
            "config": root_json["config"],
            "syscall_name": &syscall_name,
            "syscall": syscall_json,
        });

        let content = render_template(&self.handlebars, "syscall".to_string(), &template_json)
            .with_context(|| format!("Can't render syscall {}", syscall_name))?;
        output_file.write_all(content.as_bytes())?;
        Ok(())
    }
}

impl FidldocTemplate for SyscallTemplate<'_> {
    fn render_main_page(&self, _main_fidl_json: &Value) -> Result<(), Error> {
        // System calls don't do anything for the "main page". All system calls are part of the "zx"
        // library so all generation is done in render_library() below.
        Ok(())
    }

    fn render_library(&self, _package: &str, fidl_json: &Value) -> Result<(), Error> {
        let protocols = fidl_json["protocol_declarations"]
            .as_array()
            .ok_or_else(|| RenderError::new("Invalid protocol_declarations"))?;

        for protocol in protocols {
            // If the protocol is annotated with @no_protocol_prefix, then the
            // method name is the whole syscall name (modulo "zx_"); otherwise,
            // the name is split across the protocol and methods names.
            let family_name = if None == attribute_with_name(&protocol, "no_protocol_prefix") {
                // The protocol name is fully qualified and so is prefixed with
                // "$libname/".
                let name = protocol["name"].as_str().unwrap().trim().to_string();
                let (_, declname) = name.split_once("/").unwrap();
                declname.to_string()
            } else {
                String::new()
            };
            let methods = protocol["methods"]
                .as_array()
                .ok_or_else(|| RenderError::new("Invalid methods definition"))?;
            for method in methods {
                // Skip documenting any methods annotated with certain attributes.
                let mut has_prohibited = false;
                for attr in &["no_doc", "testonly", "internal"] {
                    has_prohibited = has_prohibited || (attribute_with_name(method, attr) != None);
                }

                if !has_prohibited {
                    self.render_syscall(fidl_json, &family_name, method)?;
                }
            }
        }
        Ok(())
    }

    fn name(&self) -> String {
        return "Syscall".to_string();
    }

    fn include_static_files(&self) -> Result<(), Error> {
        Ok(())
    }
}

fn render_template(
    handlebars: &Handlebars<'_>,
    template_name: String,
    fidl_json: &Value,
) -> Result<String, Error> {
    let content = handlebars
        .render(&template_name, &fidl_json)
        .with_context(|| format!("Unable to render template '{}'", template_name))?;
    Ok(content)
}

#[cfg(test)]
mod test {
    use serde_json::json;
    #[test]
    fn extract_item_docstring_test() {
        let item_json = json!({
          "maybe_attributes": [
            json!({
              "name": "some_other_attribute",
            }),
            json!({
              "name": "doc",
              "arguments": [
                json!({
                  "name": "value",
                  "type": "string",
                  "value": json!({
                    "kind": "literal",
                    "value": " Reroute the subroutine\n\n This function reroutes it.",
                    "expression": "/// Reroute the subroutine\n///\n/// This function reroutes it.",
                  }),
                })
              ],
            }),
          ],
        });

        let docstring = crate::templates::syscall::extract_item_docstring(&item_json);
        assert_eq!(docstring, "Reroute the subroutine\n\nThis function reroutes it.");
    }
}
