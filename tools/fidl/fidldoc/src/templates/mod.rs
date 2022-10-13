// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

pub mod markdown;
pub mod syscall;

use crate::fidljson::to_lower_snake_case;
use anyhow::Error;
use handlebars::{Context, Handlebars, Helper, JsonRender, Output, RenderContext, RenderError};
use lazy_static::lazy_static;
use pulldown_cmark::{html as pulldown_html, Parser};
use regex::{Captures, Regex};
use serde_json::Value;
use tracing::debug;

pub fn lower_snake_case(
    h: &Helper<'_, '_>,
    _: &Handlebars<'_>,
    _: &Context,
    _: &mut RenderContext<'_, '_>,
    out: &mut dyn Output,
) -> Result<(), RenderError> {
    // get parameter from helper or throw an error
    let param = h
        .param(0)
        .ok_or_else(|| RenderError::new("Param 0 is required for lower_snake_case helper"))?;
    debug!("lower_snake_case called on {}", param.value().render());
    out.write(&to_lower_snake_case(&param.value().render()))?;
    Ok(())
}

pub trait FidldocTemplate {
    fn render_main_page(&self, main_fidl_json: &Value) -> Result<(), Error>;
    fn render_library(&self, package: &str, fidl_json: &Value) -> Result<(), Error>;
    fn name(&self) -> String;
    fn include_static_files(&self) -> Result<(), Error>;
}

pub type HandlebarsHelper = fn(
    h: &Helper<'_, '_>,
    _: &Handlebars<'_>,
    _: &Context,
    _: &mut RenderContext<'_, '_>,
    out: &mut dyn Output,
) -> Result<(), RenderError>;

pub fn get_link_helper(
    h: &Helper<'_, '_>,
    _: &Handlebars<'_>,
    _: &Context,
    _: &mut RenderContext<'_, '_>,
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
    h: &Helper<'_, '_>,
    _: &Handlebars<'_>,
    _: &Context,
    _: &mut RenderContext<'_, '_>,
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
    h: &Helper<'_, '_>,
    _: &Handlebars<'_>,
    _: &Context,
    _: &mut RenderContext<'_, '_>,
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
    h: &Helper<'_, '_>,
    _: &Handlebars<'_>,
    _: &Context,
    _: &mut RenderContext<'_, '_>,
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

fn len(
    h: &Helper<'_, '_>,
    _: &Handlebars<'_>,
    _: &Context,
    _: &mut RenderContext<'_, '_>,
    out: &mut dyn Output,
) -> Result<(), RenderError> {
    // get parameter from helper or throw an error
    let vector =
        h.param(0).ok_or_else(|| RenderError::new("Param 0 is required for len helper"))?;
    if let &Value::Array(ref array) = vector.value() {
        out.write(array.len().to_string().as_str())?;
        Ok(())
    } else {
        Err(RenderError::new("Param 0 is not an array"))
    }
}

fn doc_link(
    h: &Helper<'_, '_>,
    _: &Handlebars<'_>,
    _: &Context,
    _: &mut RenderContext<'_, '_>,
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
    RE.replace_all(&docstring, |caps: &Captures<'_>| {
        let package = caps.get(1).unwrap().as_str();
        debug!("dl captured {}", package);
        pl(package, base)
    })
    .to_string()
}

pub fn remove_parent_folders(
    h: &Helper<'_, '_>,
    _: &Handlebars<'_>,
    _: &Context,
    _: &mut RenderContext<'_, '_>,
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
    h: &Helper<'_, '_>,
    _: &Handlebars<'_>,
    _: &Context,
    _: &mut RenderContext<'_, '_>,
    out: &mut dyn Output,
) -> Result<(), RenderError> {
    let config = h.param(0).ok_or_else(|| RenderError::new("Param 0 is required for sl helper"))?;
    let tag = h.param(1).ok_or_else(|| RenderError::new("Param 1 is required for sl helper"))?;
    let location =
        h.param(2).ok_or_else(|| RenderError::new("Param 2 is required for sl helper"))?;
    debug!(
        "source_link called on {}, {} and {}",
        config.value().to_string(),
        tag.value().to_string(),
        location.value().to_string()
    );
    out.write(&sl(&config.value(), &tag.value(), &location.value()))?;
    Ok(())
}

fn sl(config: &Value, tag: &Value, location: &Value) -> String {
    if location.get("line").is_some() {
        // Output source link with line number
        format!(
            "{baseUrl}{tag}{filenamePrefix}{filename}{linePrefix}{lineNo}",
            baseUrl = config["source"]["baseUrl"].as_str().unwrap(),
            tag = tag.as_str().unwrap(),
            filenamePrefix = config["source"]["filenamePrefix"].as_str().unwrap(),
            filename = rpf(location["filename"].as_str().unwrap()),
            linePrefix = config["source"]["line"].as_str().unwrap(),
            lineNo = location["line"]
        )
    } else {
        // Output general source link, without line number
        format!(
            "{baseUrl}{tag}{filenamePrefix}{filename}",
            baseUrl = config["source"]["baseUrl"].as_str().unwrap(),
            tag = tag.as_str().unwrap(),
            filenamePrefix = config["source"]["filenamePrefix"].as_str().unwrap(),
            filename = rpf(location["filename"].as_str().unwrap())
        )
    }
}

pub fn one_line(
    h: &Helper<'_, '_>,
    _: &Handlebars<'_>,
    _: &Context,
    _: &mut RenderContext<'_, '_>,
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
    h: &Helper<'_, '_>,
    _: &Handlebars<'_>,
    _: &Context,
    _: &mut RenderContext<'_, '_>,
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

pub fn method_id(
    h: &Helper<'_, '_>,
    _: &Handlebars<'_>,
    _: &Context,
    _: &mut RenderContext<'_, '_>,
    out: &mut dyn Output,
) -> Result<(), RenderError> {
    let method_name =
        h.param(0).ok_or_else(|| RenderError::new("Param 0 is required for method_id helper"))?;
    let protocol_name =
        h.param(1).ok_or_else(|| RenderError::new("Param 1 is required for method_id helper"))?;
    debug!(
        "method_id called on {} and {}",
        method_name.value().to_string(),
        protocol_name.value().to_string()
    );
    out.write(&mi(&method_name.value().render(), &protocol_name.value().render()))?;
    Ok(())
}

fn mi(method: &str, protocol: &str) -> String {
    format!("{protocol}.{method}", protocol = protocol, method = method)
}

pub fn process_versions(
    h: &Helper<'_, '_>,
    _: &Handlebars<'_>,
    _: &Context,
    _: &mut RenderContext<'_, '_>,
    out: &mut dyn Output,
) -> Result<(), RenderError> {
    let version_list: Value = serde_json::from_str(
        &h.param(0)
            .ok_or_else(|| RenderError::new("Param 0 is required for process_version helper"))?
            .value()
            .to_string(),
    )?;

    let output = pv(&version_list);
    out.write(&output)?;
    Ok(())
}

fn pv(version_list: &Value) -> String {
    let mut added = "".to_string();
    let mut deprecated = "".to_string();
    let mut removed = "".to_string();
    let mut version_val;

    // Each iteration picks up an 'added', 'removed' or 'deprecated'
    // version which can be unordered.
    // Loop over all entries to pick up all the information.
    // If any value is 2^64-2, render it as HEAD
    // TODO(theosiu) Update to have links for release notes
    for version in version_list.as_array().expect("get array of versions") {
        version_val = if version["value"]["value"] == (crate::HEAD_VERSION_NUMBER).to_string() {
            crate::HEAD_VERSION.to_string()
        } else {
            version["value"]["value"].to_string().trim_matches('"').to_string()
        };

        if version["name"] == "added" {
            added = format!(
                "<span class=\"fidl-attribute fidl-version\">Added: {version_val}</span>",
                version_val = version_val
            );
        } else if version["name"] == "removed" {
            removed = format!(
                "<span class=\"fidl-attribute fidl-version\">Removed: {version_val}</span>",
                version_val = version_val
            );
        } else if version["name"] == "deprecated" {
            deprecated = format!(
                "<span class=\"fidl-attribute fidl-version\">Deprecated: {version_val}</span>",
                version_val = version_val
            );
        }
    }
    return format!(
        // The order is backward to allow float right to show Added, Deprecated, Removed
        "{removed} {deprecated} {added}",
        added = added,
        removed = removed,
        deprecated = deprecated
    )
    .trim()
    .to_string();
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
            "tag": "HEAD",
            "config": json!({
                "source": json!({
                    "baseUrl": "https://example.com/",
                    "filenamePrefix": ":",
                    "line": "#"
                })
            })
        });

        let location_line = json!({
            "filename": "sample.fidl",
            "line": 42
        });

        assert_eq!(
            sl(&fidl_json["config"], &fidl_json["tag"], &location_line),
            "https://example.com/HEAD:sample.fidl#42"
        );

        let location_no_line = json!({
            "filename": "foobar.fidl"
        });

        assert_eq!(
            sl(&fidl_json["config"], &fidl_json["tag"], &location_no_line),
            "https://example.com/HEAD:foobar.fidl"
        );

        let location_with_folders = json!({
            "filename": "../../sdk/fidl/fuchsia.bluetooth/address.fidl"
        });

        assert_eq!(
            sl(&fidl_json["config"], &fidl_json["tag"], &location_with_folders),
            "https://example.com/HEAD:sdk/fidl/fuchsia.bluetooth/address.fidl"
        );

        let location_with_folders_and_line = json!({
            "filename": "../../sdk/fidl/fuchsia.bluetooth/address.fidl",
            "line": 9
        });

        assert_eq!(
            sl(&fidl_json["config"], &fidl_json["tag"], &location_with_folders_and_line),
            "https://example.com/HEAD:sdk/fidl/fuchsia.bluetooth/address.fidl#9"
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

    #[test]
    fn pv_test() {
        let fidl_json1 = json!([
            json!({"value": json!({"value": "1"}),
            "name": "added",}),
            json!({"value": json!({"value": "3"}),
            "name": "removed",}),
            json!({"value": json!({"value": "2"}),
            "name": "deprecated",}),
        ]);
        assert_eq!(pv(&fidl_json1), "<span class=\"fidl-attribute fidl-version\">Removed: 3</span> <span class=\"fidl-attribute fidl-version\">Deprecated: 2</span> <span class=\"fidl-attribute fidl-version\">Added: 1</span>");

        let fidl_json2 = json!([
            json!({"value": json!({"value": "1"}),
            "name": "added",}),
            json!({"value": json!({"value": "2"}),
            "name": "deprecated",}),
        ]);
        assert_eq!(pv(&fidl_json2), "<span class=\"fidl-attribute fidl-version\">Deprecated: 2</span> <span class=\"fidl-attribute fidl-version\">Added: 1</span>");

        let fidl_json3 = json!([json!({"value": json!({"value": "1"}),
            "name": "added",})]);
        assert_eq!(pv(&fidl_json3), "<span class=\"fidl-attribute fidl-version\">Added: 1</span>");
    }

    #[test]
    fn mi_test() {
        let method = "Clone";
        let protocol = "Directory";
        let expected = "Directory.Clone";
        assert_eq!(mi(method, protocol), expected.to_string());
    }
}
