// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

pub mod html;
pub mod markdown;

use failure::Error;
use handlebars::{Context, Handlebars, Helper, JsonRender, Output, RenderContext, RenderError};
use lazy_static::lazy_static;
use log::debug;
use pulldown_cmark::{html as pulldown_html, Parser};
use regex::{Captures, Regex};
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
    out: &mut dyn Output,
) -> Result<(), RenderError>;

pub fn get_link_helper(
    h: &Helper,
    _: &Handlebars,
    _: &Context,
    _: &mut RenderContext,
    out: &mut dyn Output,
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
    out: &mut dyn Output,
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
    out: &mut dyn Output,
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
    out: &mut dyn Output,
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
    let package_base;
    let package_name;

    lazy_static! {
        static ref RE: Regex = Regex::new(r"(.*)/(.*)").unwrap();
    }

    if RE.is_match(&name) {
        // e.g. "fuchsia.media/Audio"
        let caps = RE.captures(&name).expect("Expecting base/package");
        package_base = caps.get(1).unwrap().as_str();
        package_name = caps.get(2).unwrap().as_str();
    } else {
        // e.g. "Audio"
        package_base = &base;
        package_name = &name;
    }

    if package_base == base {
        format!("<a class='link' href='#{anchor}'>{anchor}</a>", anchor = package_name)
    } else {
        format!("<a class='link' href='../{b}/'>{b}</a>/<a class='link' href='../{b}/#{anchor}'>{anchor}</a>", b = package_base, anchor = package_name)
    }
}

fn doc_link(
    h: &Helper,
    _: &Handlebars,
    _: &Context,
    _: &mut RenderContext,
    out: &mut dyn Output,
) -> Result<(), RenderError> {
    // get parameter from helper or throw an error
    let docs =
        h.param(0).ok_or_else(|| RenderError::new("Param 0 is required for doc_link helper"))?;
    let base =
        h.param(1).ok_or_else(|| RenderError::new("Param 1 is required for doc_link helper"))?;
    let docstring = docs.value().render();
    debug!("doc_link called on {} and {}", docstring, base.value().render());
    out.write(&dl(&docstring, &base.value().render()))?;
    Ok(())
}

fn dl(docstring: &str, base: &str) -> String {
    lazy_static! {
        static ref RE: Regex = Regex::new(r"\[`(.*?)`\]").unwrap();
    }
    RE.replace_all(&docstring, |caps: &Captures| {
        let package = caps.get(1).unwrap().as_str();
        debug!("dl captured {}", package);
        pl(package, base)
    })
    .to_string()
}

pub fn remove_parent_folders(
    h: &Helper,
    _: &Handlebars,
    _: &Context,
    _: &mut RenderContext,
    out: &mut dyn Output,
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

fn source_link(
    h: &Helper,
    _: &Handlebars,
    _: &Context,
    _: &mut RenderContext,
    out: &mut dyn Output,
) -> Result<(), RenderError> {
    let fidl_json =
        h.param(0).ok_or_else(|| RenderError::new("Param 0 is required for sl helper"))?;
    let location =
        h.param(1).ok_or_else(|| RenderError::new("Param 1 is required for sl helper"))?;
    debug!(
        "source_link called on {} and {}",
        fidl_json.value().to_string(),
        location.value().to_string()
    );
    out.write(&sl(&fidl_json.value(), &location.value()))?;
    Ok(())
}

fn sl(fidl_json: &Value, location: &Value) -> String {
    if location.get("line").is_some() {
        // Output source link with line number
        format!(
            "{baseUrl}{tag}/{filename}{linePrefix}{lineNo}",
            baseUrl = fidl_json["config"]["source"]["baseUrl"].as_str().unwrap(),
            tag = fidl_json["tag"].as_str().unwrap(),
            filename = rpf(location["filename"].as_str().unwrap()),
            linePrefix = fidl_json["config"]["source"]["line"].as_str().unwrap(),
            lineNo = location["line"]
        )
    } else {
        // Output general source link, without line number
        format!(
            "{baseUrl}{tag}/{filename}",
            baseUrl = fidl_json["config"]["source"]["baseUrl"].as_str().unwrap(),
            tag = fidl_json["tag"].as_str().unwrap(),
            filename = rpf(location["filename"].as_str().unwrap())
        )
    }
}

pub fn one_line(
    h: &Helper,
    _: &Handlebars,
    _: &Context,
    _: &mut RenderContext,
    out: &mut dyn Output,
) -> Result<(), RenderError> {
    // get parameter from helper or throw an error
    let param =
        h.param(0).ok_or_else(|| RenderError::new("Param 0 is required for oneline helper"))?;
    debug!("oneline called on {}", param.value().render());
    out.write(&ol(&param.value().render()))?;
    Ok(())
}

fn ol(description: &str) -> String {
    let lines: Vec<&str> = description.split("\n\n").collect();
    // If no match for \n\n, lines will contain the description as is.
    lines.first().unwrap().to_string()
}

pub fn pulldown(
    h: &Helper,
    _: &Handlebars,
    _: &Context,
    _: &mut RenderContext,
    out: &mut dyn Output,
) -> Result<(), RenderError> {
    // get parameter from helper or throw an error
    let param =
        h.param(0).ok_or_else(|| RenderError::new("Param 0 is required for pulldown helper"))?;
    debug!("pulldown called on {}", param.value().render());
    out.write(&pd(&param.value().render()))?;
    Ok(())
}

fn pd(text: &str) -> String {
    let parser = Parser::new(text);
    let mut html_text = String::new();
    pulldown_html::push_html(&mut html_text, parser);
    html_text
}

#[cfg(test)]
mod test {
    use super::*;
    use serde_json::json;

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
        let expected = "<a class='link' href='#Audio'>Audio</a>";
        assert_eq!(pl(name, base), expected.to_string());

        let name = "fuchsia.media/Metadata";
        let base = "fuchsia.media.sessions";
        let expected = "<a class='link' href='../fuchsia.media/'>fuchsia.media</a>/<a class='link' href='../fuchsia.media/#Metadata'>Metadata</a>";
        assert_eq!(pl(name, base), expected.to_string());

        let name = "Audio";
        let base = "fuchsia.media";
        let expected = "<a class='link' href='#Audio'>Audio</a>";
        assert_eq!(pl(name, base), expected.to_string());
    }

    #[test]
    fn dl_test() {
        let docs = "This is the the documentation for [`fuchsia.media/Audio`]";
        let base = "fuchsia.media";
        let expected = "This is the the documentation for <a class='link' href='#Audio'>Audio</a>";
        assert_eq!(dl(docs, base), expected.to_string());

        let docs = "This is [`fuchsia.media/Audio`] and this is [`fuchsia.media.sessions/Publisher`], while this is not a link to fuchsia.io/Node.";
        let base = "fuchsia.media.sessions";
        let expected = "This is <a class='link' href='../fuchsia.media/'>fuchsia.media</a>/<a class='link' href='../fuchsia.media/#Audio'>Audio</a> and this is <a class='link' href='#Publisher'>Publisher</a>, while this is not a link to fuchsia.io/Node.";
        assert_eq!(dl(docs, base), expected.to_string());
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

    #[test]
    fn sl_test() {
        let fidl_json = json!({
            "tag": "master",
            "config": json!({
                "source": json!({
                    "baseUrl": "https://example.com/",
                    "line": "#"
                })
            })
        });

        let location_line = json!({
            "filename": "sample.fidl",
            "line": 42
        });

        assert_eq!(sl(&fidl_json, &location_line), "https://example.com/master/sample.fidl#42");

        let location_no_line = json!({
            "filename": "foobar.fidl"
        });

        assert_eq!(sl(&fidl_json, &location_no_line), "https://example.com/master/foobar.fidl");

        let location_with_folders = json!({
            "filename": "../../sdk/fidl/fuchsia.bluetooth/address.fidl"
        });

        assert_eq!(
            sl(&fidl_json, &location_with_folders),
            "https://example.com/master/sdk/fidl/fuchsia.bluetooth/address.fidl"
        );

        let location_with_folders_and_line = json!({
            "filename": "../../sdk/fidl/fuchsia.bluetooth/address.fidl",
            "line": 9
        });

        assert_eq!(
            sl(&fidl_json, &location_with_folders_and_line),
            "https://example.com/master/sdk/fidl/fuchsia.bluetooth/address.fidl#9"
        );
    }

    #[test]
    fn ol_test() {
        let description = "some text\nmore text\n\nto be ignored\n";
        assert_eq!(ol(description), "some text\nmore text");

        let no_newlines = "some text";
        assert_eq!(ol(no_newlines), "some text");

        let multiple_newlines = "some text\n\nmore text\n\neven more text";
        assert_eq!(ol(multiple_newlines), "some text");
    }

    #[test]
    fn pd_test() {
        let description = r#"In addition, clients may assert the type of the object by setting
the protocol corresponding to the expected type:
* If the caller expected a directory but the node cannot be accessed
  as a directory, the error is `ZX_ERR_NOT_DIR`.
* If the caller expected a file but the node cannot be accessed as a
  file, the error is `ZX_ERR_NOT_FILE`.
* In other mismatched cases, the error is `ZX_ERR_WRONG_TYPE`.
"#;

        let expected = r#"<p>In addition, clients may assert the type of the object by setting
the protocol corresponding to the expected type:</p>
<ul>
<li>If the caller expected a directory but the node cannot be accessed
as a directory, the error is <code>ZX_ERR_NOT_DIR</code>.</li>
<li>If the caller expected a file but the node cannot be accessed as a
file, the error is <code>ZX_ERR_NOT_FILE</code>.</li>
<li>In other mismatched cases, the error is <code>ZX_ERR_WRONG_TYPE</code>.</li>
</ul>
"#;
        assert_eq!(pd(description), expected);
    }
}
