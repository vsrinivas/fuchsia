// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::Result;
use std::io::Write;

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
    fn add_test_case<'a>(&'a mut self, marker: &str) -> &'a dyn TestCodeBuilder;
    fn add_mock_impl<'a>(
        &'a mut self,
        component_name: &str,
        protocol: &str,
    ) -> &'a dyn TestCodeBuilder;
}
