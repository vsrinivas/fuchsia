// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::Result;
use chrono::Datelike;
use std::io::Write;

const COPYRIGHT: &'static str = include_str!("templates/template_copyright");

pub trait CodeGenerator {
    fn write_file<W: Write>(&self, writer: &mut W) -> Result<()>;
}

pub trait TestCodeBuilder {
    fn new(component_name: &str) -> Self
    where
        Self: Sized;
    fn add_import<'a>(&'a mut self, import_library: &str) -> &'a dyn TestCodeBuilder;
    fn add_component<'a>(
        &'a mut self,
        component_name: &str,
        url: &str,
        const_var: &str,
        mock: bool,
    ) -> &'a dyn TestCodeBuilder;
    fn add_protocol<'a>(
        &'a mut self,
        protocol: &str,
        source: &str,
        targets: Vec<String>,
    ) -> &'a dyn TestCodeBuilder;
    fn add_directory<'a>(
        &'a mut self,
        dir_name: &str,
        dir_path: &str,
        targets: Vec<String>,
    ) -> &'a dyn TestCodeBuilder;
    fn add_storage<'a>(
        &'a mut self,
        storage_name: &str,
        storage_path: &str,
        targets: Vec<String>,
    ) -> &'a dyn TestCodeBuilder;
    fn add_test_case<'a>(&'a mut self, protocol: &str) -> &'a dyn TestCodeBuilder;
    fn add_fidl_connect<'a>(&'a mut self, protocol: &str) -> &'a dyn TestCodeBuilder;
    fn add_mock_impl<'a>(
        &'a mut self,
        component_name: &str,
        protocol: &str,
    ) -> &'a dyn TestCodeBuilder;
}

pub fn convert_to_camel(input: &str) -> String {
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

pub fn copyright(comment: &str) -> String {
    let current_date = chrono::Utc::now();
    let year = current_date.year();
    return COPYRIGHT.replace("CURRENT_YEAR", &year.to_string()).replace("COMMENT_START", comment);
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
