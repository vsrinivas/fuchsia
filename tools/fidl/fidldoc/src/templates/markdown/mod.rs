// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use failure::{Error, ResultExt};
use handlebars::Handlebars;
use log::info;
use serde_json::Value;
use std::fs;
use std::fs::File;
use std::io::Write;
use std::path::PathBuf;

use crate::templates::{FidldocTemplate, HandlebarsHelper};

pub struct MarkdownTemplate {
    handlebars: Handlebars,
    output_path: PathBuf,
}

impl MarkdownTemplate {
    pub fn new(output_path: &PathBuf) -> MarkdownTemplate {
        // Handlebars
        let mut handlebars = Handlebars::new();

        // Register partials
        for &(name, template, expect) in &[
            ("header", include_str!("partials/header.hbs"), "Failed to include header"),
            ("header_dir", include_str!("partials/header_dir.hbs"), "Failed to include header_dir"),
            ("footer", include_str!("partials/footer.hbs"), "Failed to include footer"),
        ] {
            handlebars.register_template_string(name, template).expect(expect);
        }

        // Register FIDL declarations partials
        for &(name, template, expect) in &[
            (
                "protocols",
                include_str!("partials/declarations/protocols.hbs"),
                "Failed to include protocols",
            ),
            (
                "structs",
                include_str!("partials/declarations/structs.hbs"),
                "Failed to include structs",
            ),
            ("enums", include_str!("partials/declarations/enums.hbs"), "Failed to include enums"),
            (
                "tables",
                include_str!("partials/declarations/tables.hbs"),
                "Failed to include tables",
            ),
            (
                "unions",
                include_str!("partials/declarations/unions.hbs"),
                "Failed to include unions",
            ),
            (
                "xunions",
                include_str!("partials/declarations/xunions.hbs"),
                "Failed to include xunions",
            ),
            ("bits", include_str!("partials/declarations/bits.hbs"), "Failed to include bits"),
            (
                "constants",
                include_str!("partials/declarations/constants.hbs"),
                "Failed to include constants",
            ),
            (
                "type_aliases",
                include_str!("partials/declarations/type_aliases.hbs"),
                "Failed to include type_aliases",
            ),
        ] {
            handlebars.register_template_string(name, template).expect(expect);
        }

        // Register FIDL type partials
        for &(name, template, expect) in &[
            ("doc", include_str!("partials/types/doc.hbs"), "Failed to include doc"),
            ("filename", include_str!("partials/types/filename.hbs"), "Failed to include filename"),
            ("type", include_str!("partials/types/type.hbs"), "Failed to include type"),
            ("vector", include_str!("partials/types/vector.hbs"), "Failed to include vector"),
        ] {
            handlebars.register_template_string(name, template).expect(expect);
        }

        // Register core templates
        for &(name, template, expect) in &[
            ("main", include_str!("main.hbs"), "Failed to include main"),
            ("interface", include_str!("interface.hbs"), "Failed to include interface"),
            ("toc", include_str!("toc.hbs"), "Failed to include toc"),
        ] {
            handlebars.register_template_string(name, template).expect(expect);
        }

        // Register helpers
        let helpers: &[(&str, HandlebarsHelper)] = &[
            ("getLink", crate::templates::get_link_helper),
            ("rpn", crate::templates::remove_package_name),
            ("eq", crate::templates::eq),
            ("pl", crate::templates::package_link),
            ("rpf", crate::templates::remove_parent_folders),
            ("sl", crate::templates::source_link),
            ("docLink", crate::templates::doc_link),
            ("oneline", crate::templates::one_line),
            ("pulldown", crate::templates::pulldown),
        ];
        for &(name, helper) in helpers {
            handlebars.register_helper(name, Box::new(helper));
        }

        MarkdownTemplate { handlebars: handlebars, output_path: output_path.to_path_buf() }
    }
}

impl FidldocTemplate for MarkdownTemplate {
    fn render_main_page(&self, main_fidl_json: &Value) -> Result<(), Error> {
        // Render main page
        let main_page_path = self.output_path.join("README.md");
        info!("Generating main page documentation {}", main_page_path.display());

        let mut main_page_file = File::create(&main_page_path)
            .with_context(|e| format!("Can't create {}: {}", main_page_path.display(), e))?;

        let main_page_content =
            render_template(&self.handlebars, "main".to_string(), &main_fidl_json).with_context(
                |e| format!("Can't render main page {}: {}", main_page_path.display(), e),
            )?;
        main_page_file.write_all(main_page_content.as_bytes())?;

        // Render TOC
        let toc_path = self.output_path.join("_toc.yaml");
        info!("Generating TOC {}", toc_path.display());

        let mut toc_file = File::create(&toc_path)
            .with_context(|e| format!("Can't create TOC {}: {}", toc_path.display(), e))?;

        let toc_content = render_template(&self.handlebars, "toc".to_string(), &main_fidl_json)
            .with_context(|e| format!("Can't render TOC {}: {}", toc_path.display(), e))?;
        toc_file.write_all(toc_content.as_bytes())?;

        Ok(())
    }

    fn render_interface(&self, package: &str, fidl_json: &Value) -> Result<(), Error> {
        // Create a directory for interface files
        let output_dir = self.output_path.join(package);
        fs::create_dir(&output_dir)?;
        info!("Created output directory {}", output_dir.display());

        let package_index = output_dir.join("README.md");
        info!("Generating package documentation {}", package_index.display());

        let mut output_file = File::create(&package_index)
            .with_context(|e| format!("Can't create {}: {}", package_index.display(), e))?;

        // Render files
        let package_content =
            render_template(&self.handlebars, "interface".to_string(), &fidl_json)
                .with_context(|e| format!("Can't render interface {}: {}", package, e))?;
        output_file.write_all(package_content.as_bytes())?;

        Ok(())
    }

    fn name(&self) -> String {
        return "Markdown".to_string();
    }
}

fn render_template(
    handlebars: &Handlebars,
    template_name: String,
    fidl_json: &Value,
) -> Result<String, Error> {
    let content = handlebars
        .render(&template_name, &fidl_json)
        .with_context(|e| format!("Unable to render template '{}': {}", template_name, e))?;
    Ok(content)
}

#[cfg(test)]
mod test {
    use super::*;
    use serde_json::json;
    #[test]
    fn render_main_page_test() {
        let source = include_str!("testdata/README.md");

        let mut table_of_contents: Vec<crate::TableOfContentsItem> = vec![];
        table_of_contents.push(crate::TableOfContentsItem {
            name: "fuchsia.auth".to_string(),
            link: "fuchsia.auth/README.md".to_string(),
            description: "Fuchsia Auth API".to_string(),
        });
        table_of_contents.push(crate::TableOfContentsItem {
            name: "fuchsia.media".to_string(),
            link: "fuchsia.media/README.md".to_string(),
            description: "Fuchsia Media API".to_string(),
        });

        let fidl_config = json!(null);
        let declarations: Vec<String> = Vec::new();

        let main_fidl_doc = json!({
            "table_of_contents": table_of_contents,
            "fidldoc_version": "0.0.4",
            "config": fidl_config,
            "search": declarations,
            "url_path": "/",
        });

        let template = MarkdownTemplate::new(&PathBuf::new());

        let result = render_template(&template.handlebars, "main".to_string(), &main_fidl_doc)
            .expect("Unable to render main template");
        assert_eq!(result, source);
    }

    #[test]
    fn render_declarations_test() {
        for &(source, testdata, template_name) in &[
            (
                include_str!("testdata/bits_declarations.md"),
                include_str!("testdata/bits_declarations.json"),
                "bits",
            ),
            (
                include_str!("testdata/constants_declarations.md"),
                include_str!("testdata/constants_declarations.json"),
                "constants",
            ),
            (
                include_str!("testdata/enum_declarations.md"),
                include_str!("testdata/enum_declarations.json"),
                "enums",
            ),
            (
                include_str!("testdata/protocols_declarations.md"),
                include_str!("testdata/protocols_declarations.json"),
                "protocols",
            ),
            (
                include_str!("testdata/structs_declarations.md"),
                include_str!("testdata/structs_declarations.json"),
                "structs",
            ),
            (
                include_str!("testdata/tables_declarations.md"),
                include_str!("testdata/tables_declarations.json"),
                "tables",
            ),
            (
                include_str!("testdata/type_aliases_declarations.md"),
                include_str!("testdata/type_aliases_declarations.json"),
                "type_aliases",
            ),
            (
                include_str!("testdata/unions_declarations.md"),
                include_str!("testdata/unions_declarations.json"),
                "unions",
            ),
            (
                include_str!("testdata/xunions_declarations.md"),
                include_str!("testdata/xunions_declarations.json"),
                "xunions",
            ),
        ] {
            let declarations: Value = serde_json::from_str(testdata)
                .with_context(|e| format!("Unable to parse testdata for {}: {}", template_name, e))
                .unwrap();

            let template = MarkdownTemplate::new(&PathBuf::new());

            let result =
                render_template(&template.handlebars, template_name.to_string(), &declarations)
                    .with_context(|e| format!("Unable to render {} template: {}", template_name, e))
                    .unwrap();
            assert_eq!(
                result, source,
                "Generated output for template {} doesn't match goldenset",
                template_name
            );
        }
    }
}
