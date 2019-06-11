// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

pub mod html;
pub mod markdown;

use failure::Error;
use handlebars::{Context, Handlebars, Helper, JsonRender, Output, RenderContext, RenderError};
use lazy_static::lazy_static;
use log::debug;
use regex::Regex;
use serde_json::Value;

pub trait FidldocTemplate {
    fn render_main_page(&self, main_fidl_json: &Value) -> Result<(), Error>;
    fn render_interface(&self, package: &str, fidl_json: &Value) -> Result<(), Error>;
    fn name(&self) -> String;
}

pub type HandlebarsHelper = fn(
    h: &Helper,
    _: &Handlebars,
    _: &Context,
    _: &mut RenderContext,
    out: &mut Output,
) -> Result<(), RenderError>;

pub fn get_link_helper(
    h: &Helper,
    _: &Handlebars,
    _: &Context,
    _: &mut RenderContext,
    out: &mut Output,
) -> Result<(), RenderError> {
    // get parameter from helper or throw an error
    let param =
        h.param(0).ok_or_else(|| RenderError::new("Param 0 is required for get link helper"))?;
    debug!("get_link_helper called on {}", param.value().render());
    out.write(&sanitize_name(&param.value().render()))?;
    Ok(())
}

fn sanitize_name(name: &str) -> String {
    name.replace("/", "_")
}

pub fn remove_package_name(
    h: &Helper,
    _: &Handlebars,
    _: &Context,
    _: &mut RenderContext,
    out: &mut Output,
) -> Result<(), RenderError> {
    // get parameter from helper or throw an error
    let param = h.param(0).ok_or_else(|| RenderError::new("Param 0 is required for rpn helper"))?;
    debug!("remove_package_name called on {}", param.value().render());
    out.write(&rpn(&param.value().render()))?;
    Ok(())
}

fn rpn(name: &str) -> String {
    lazy_static! {
        static ref RE: Regex = Regex::new(r".*/").unwrap();
    }
    RE.replace_all(&name, "").into_owned()
}

pub fn eq(
    h: &Helper,
    _: &Handlebars,
    _: &Context,
    _: &mut RenderContext,
    out: &mut Output,
) -> Result<(), RenderError> {
    // get parameter from helper or throw an error
    let param_a = h
        .param(0)
        .ok_or_else(|| RenderError::new("Param 0 is required for eq helper"))?
        .value()
        .to_string();
    let param_b = h
        .param(1)
        .ok_or_else(|| RenderError::new("Param 1 is required for eq helper"))?
        .value()
        .to_string();
    debug!("eq called on {} and {}", param_a, param_b);
    if param_a == param_b {
        out.write("true")?;
    }
    Ok(())
}

fn package_link(
    h: &Helper,
    _: &Handlebars,
    _: &Context,
    _: &mut RenderContext,
    out: &mut Output,
) -> Result<(), RenderError> {
    // get parameter from helper or throw an error
    let package =
        h.param(0).ok_or_else(|| RenderError::new("Param 0 is required for pl helper"))?;
    let base = h.param(1).ok_or_else(|| RenderError::new("Param 1 is required for pl helper"))?;
    debug!("package_link called on {} and {}", package.value().render(), base.value().render());
    out.write(&pl(&package.value().render(), &base.value().render()))?;
    Ok(())
}

fn pl(name: &str, base: &str) -> String {
    lazy_static! {
        static ref RE: Regex = Regex::new(r"(.*)/(.*)").unwrap();
    }
    let caps = RE.captures(&name).expect("Expecting base/package");
    let package_base = caps.get(1).unwrap().as_str();
    let package_name = caps.get(2).unwrap().as_str();
    if package_base == base {
        format!(
            "<a class='link' href='../{b}/index.html#{anchor}'>{anchor}</a>",
            b = package_base,
            anchor = package_name
        )
    } else {
        format!("<a class='link' href='../{b}/index.html'>{b}</a>/<a class='link' href='../{b}/index.html#{anchor}'>{anchor}</a>", b = package_base, anchor = package_name)
    }
}

pub fn remove_parent_folders(
    h: &Helper,
    _: &Handlebars,
    _: &Context,
    _: &mut RenderContext,
    out: &mut Output,
) -> Result<(), RenderError> {
    // get parameter from helper or throw an error
    let param = h.param(0).ok_or_else(|| RenderError::new("Param 0 is required for rpf helper"))?;
    debug!("remove_parent_folders called on {}", param.value().render());
    out.write(&rpf(&param.value().render()))?;
    Ok(())
}

fn rpf(path: &str) -> String {
    lazy_static! {
        static ref RE: Regex = Regex::new(r"^(\.\./)*").unwrap();
    }
    RE.replace_all(&path, "").into_owned()
}

#[cfg(test)]
mod test {
    use super::*;

    #[test]
    fn sanitize_name_test() {
        let name = "base/name";
        assert_eq!(sanitize_name(name), "base_name");
    }

    #[test]
    fn rpn_test() {
        let name = "many/nested/levels/before/name";
        assert_eq!(rpn(name), "name");
    }

    #[test]
    fn pl_test() {
        let name = "fuchsia.media/Audio";
        let base = "fuchsia.media";
        let expected = "<a class='link' href='../fuchsia.media/index.html#Audio'>Audio</a>";
        assert_eq!(pl(name, base), expected.to_string());

        let name = "fuchsia.media/Metadata";
        let base = "fuchsia.media.sessions";
        let expected = "<a class='link' href='../fuchsia.media/index.html'>fuchsia.media</a>/<a class='link' href='../fuchsia.media/index.html#Metadata'>Metadata</a>";
        assert_eq!(pl(name, base), expected.to_string());
    }

    #[test]
    fn rpf_test() {
        let path = "../../sdk/fidl/fuchsia-web/frame.fidl";
        assert_eq!(rpf(path), "sdk/fidl/fuchsia-web/frame.fidl");

        let path = "sdk/fidl/fuchsia-web/frame.fidl";
        assert_eq!(rpf(path), "sdk/fidl/fuchsia-web/frame.fidl");

        let path = "../../sdk/fidl/../fuchsia-web/frame.fidl";
        assert_eq!(rpf(path), "sdk/fidl/../fuchsia-web/frame.fidl");
    }
}
