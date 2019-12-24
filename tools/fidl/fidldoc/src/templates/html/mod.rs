// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::{Context as _, Error};
use handlebars::{Context, Handlebars, Helper, JsonRender, Output, RenderContext, RenderError};
use lazy_static::lazy_static;
use log::info;
use regex::Regex;
use serde_json::Value;
use std::fs;
use std::fs::File;
use std::path::PathBuf;

use crate::templates::{FidldocTemplate, HandlebarsHelper};

pub struct HtmlTemplate {
    handlebars: Handlebars,
    output_path: PathBuf,
}

impl HtmlTemplate {
    pub fn new(output_path: &PathBuf) -> Result<HtmlTemplate, Error> {
        info!("The HTML template is not ready, please use Markdown");

        // Handlebars
        let mut handlebars = Handlebars::new();

        // Register core templates
        for &(name, template, expect) in &[
            ("main", include_str!("main.hbs"), "Failed to include main"),
            ("interface", include_str!("interface.hbs"), "Failed to include interface"),
        ] {
            handlebars.register_template_string(name, template).expect(expect);
        }

        // Register helpers
        let helpers: &[(&str, HandlebarsHelper)] = &[
            ("getLink", crate::templates::get_link_helper),
            ("rpn", crate::templates::remove_package_name),
            ("eq", crate::templates::eq),
            ("ph", package_hash),
            ("pl", crate::templates::package_link),
        ];
        for &(name, helper) in helpers {
            handlebars.register_helper(name, Box::new(helper));
        }

        Ok(HtmlTemplate { handlebars: handlebars, output_path: output_path.to_path_buf() })
    }
}

impl FidldocTemplate for HtmlTemplate {
    fn render_main_page(&self, main_fidl_json: &Value) -> Result<(), Error> {
        // Render main page
        let main_page_path = self.output_path.join("index.html");
        info!("Generating main page documentation {}", main_page_path.display());

        let mut main_page_file = File::create(&main_page_path)
            .with_context(|| format!("Can't create {}", main_page_path.display()))?;

        // Render main page
        self.handlebars
            .render_to_write("main", &main_fidl_json, &mut main_page_file)
            .expect("Unable to render main page");

        Ok(())
    }

    fn render_interface(&self, package: &str, fidl_json: &Value) -> Result<(), Error> {
        // Create a directory for interface files
        let output_dir = self.output_path.join(package);
        fs::create_dir(&output_dir)?;
        info!("Created output directory {}", output_dir.display());

        let package_index = output_dir.join("index.html");
        info!("Generating package documentation {}", package_index.display());

        let mut output_file = File::create(&package_index)
            .with_context(|| format!("Can't create {}", package_index.display()))?;

        // Render files
        self.handlebars
            .render_to_write("interface", &fidl_json, &mut output_file)
            .expect("Unable to render files");

        Ok(())
    }

    fn name(&self) -> String {
        return "HTML".to_string();
    }
}

fn package_hash(
    h: &Helper,
    _: &Handlebars,
    _: &Context,
    _: &mut RenderContext,
    out: &mut dyn Output,
) -> Result<(), RenderError> {
    // get parameter from helper or throw an error
    let package = h.param(0).ok_or(RenderError::new("Param 0 is required for ph helper"))?;
    let package_only =
        h.param(1).ok_or_else(|| RenderError::new("Param 1 is required for ph helper"))?;
    out.write(&ph(&package.value().render(), package_only.value().render() == "true"))?;
    Ok(())
}

fn ph(name: &str, package_only: bool) -> String {
    lazy_static! {
        static ref RE: Regex = Regex::new(r"(.*)/(.*)").unwrap();
    }
    if package_only {
        // Do not add the location hash
        RE.replace_all(&name, "../$1/index.html").into_owned()
    } else {
        // Add a location hash
        RE.replace_all(&name, "../$1/index.html#$2").into_owned()
    }
}

#[test]
fn ph_test() {
    let name = "fuchsia.media/Audio";

    let without_hash = "../fuchsia.media/index.html";
    assert_eq!(ph(name, true), without_hash.to_string());

    let with_hash = "../fuchsia.media/index.html#Audio";
    assert_eq!(ph(name, false), with_hash.to_string());
}
