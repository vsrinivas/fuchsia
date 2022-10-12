// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::{Context as _, Error};
use handlebars::Handlebars;
use serde_json::Value;
use std::fs;
use std::fs::File;
use std::io::Write;
use std::path::PathBuf;
use tracing::info;

use crate::templates::{FidldocTemplate, HandlebarsHelper};

pub struct MarkdownTemplate<'a> {
    handlebars: Handlebars<'a>,
    output_path: PathBuf,
}

impl<'a> MarkdownTemplate<'a> {
    pub fn new(output_path: &PathBuf) -> MarkdownTemplate<'a> {
        // Handlebars
        let mut handlebars = Handlebars::new();

        // Register partials
        for &(name, template, expect) in &[
            ("header", include_str!("partials/header.hbs"), "Failed to include header"),
            ("header_dir", include_str!("partials/header_dir.hbs"), "Failed to include header_dir"),
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
            ("bits", include_str!("partials/declarations/bits.hbs"), "Failed to include bits"),
            (
                "constants",
                include_str!("partials/declarations/constants.hbs"),
                "Failed to include constants",
            ),
            (
                "aliases",
                include_str!("partials/declarations/aliases.hbs"),
                "Failed to include aliases",
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
            ("library", include_str!("library.hbs"), "Failed to include library"),
            ("toc", include_str!("toc.hbs"), "Failed to include toc"),
        ] {
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
            ("lower_snake_case", crate::templates::lower_snake_case),
            ("pulldown", crate::templates::pulldown),
            ("methodId", crate::templates::method_id),
            ("processVersions", crate::templates::process_versions),
        ];
        for &(name, helper) in helpers {
            handlebars.register_helper(name, Box::new(helper));
        }

        MarkdownTemplate { handlebars: handlebars, output_path: output_path.to_path_buf() }
    }
}

impl FidldocTemplate for MarkdownTemplate<'_> {
    fn render_main_page(&self, main_fidl_json: &Value) -> Result<(), Error> {
        // Render main page
        let main_page_path = self.output_path.join("README.md");
        info!("Generating main page documentation {}", main_page_path.display());

        let mut main_page_file = File::create(&main_page_path)
            .with_context(|| format!("Can't create {}", main_page_path.display()))?;

        let main_page_content =
            render_template(&self.handlebars, "main".to_string(), &main_fidl_json)
                .with_context(|| format!("Can't render main page {}", main_page_path.display()))?;
        main_page_file.write_all(main_page_content.as_bytes())?;

        // Render TOC
        let toc_path = self.output_path.join("_toc.yaml");
        info!("Generating TOC {}", toc_path.display());

        let mut toc_file = File::create(&toc_path)
            .with_context(|| format!("Can't create TOC {}", toc_path.display()))?;

        let toc_content = render_template(&self.handlebars, "toc".to_string(), &main_fidl_json)
            .with_context(|| format!("Can't render TOC {}", toc_path.display()))?;
        toc_file.write_all(toc_content.as_bytes())?;

        Ok(())
    }

    fn render_library(&self, package: &str, fidl_json: &Value) -> Result<(), Error> {
        let output_dir = self.output_path.join(package);
        fs::create_dir(&output_dir)?;
        info!("Created output directory {}", output_dir.display());

        let package_index = output_dir.join("README.md");
        info!("Generating package documentation {}", package_index.display());

        let mut output_file = File::create(&package_index)
            .with_context(|| format!("Can't create {}", package_index.display()))?;

        // Render files
        let package_content = render_template(&self.handlebars, "library".to_string(), &fidl_json)
            .with_context(|| format!("Can't render library {}", package))?;
        output_file.write_all(package_content.as_bytes())?;

        Ok(())
    }

    fn name(&self) -> String {
        return "Markdown".to_string();
    }

    fn include_static_files(&self) -> Result<(), Error> {
        let style_css_path = self.output_path.join("style.css");
        info!("Copying style.css to {}", style_css_path.display());

        let mut style_css_file = File::create(&style_css_path)
            .with_context(|| format!("Can't create {}", style_css_path.display()))?;

        style_css_file.write_all(include_str!("style.css").as_bytes())?;

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
    use super::*;
    use serde_json::json;
    #[test]
    fn render_main_page_test() {
        let source = include_str!("testdata/README.md");

        let mut table_of_contents_items: Vec<crate::TableOfContentsItem> = vec![];
        table_of_contents_items.push(crate::TableOfContentsItem {
            name: "fuchsia.auth".to_string(),
            link: "fuchsia.auth/README.md".to_string(),
            description: "Fuchsia Auth API".to_string(),
            added: "7".to_string(),
        });
        table_of_contents_items.push(crate::TableOfContentsItem {
            name: "fuchsia.media".to_string(),
            link: "fuchsia.media/README.md".to_string(),
            description: "Fuchsia Media API".to_string(),
            added: "".to_string(),
        });

        let table_of_contents = crate::TableOfContents {
            items: table_of_contents_items,
            versions: vec!["7".to_string(), "HEAD".to_string()],
            default_version: "7".to_string(),
        };

        let fidl_config = json!(null);
        let declarations: Vec<String> = Vec::new();

        let main_fidl_doc = json!({
            "table_of_contents": table_of_contents,
            "config": fidl_config,
            "search": declarations,
            "url_path": "/",
        });

        let pb = PathBuf::new();
        let template = MarkdownTemplate::new(&pb);

        let result = render_template(&template.handlebars, "main".to_string(), &main_fidl_doc)
            .expect("Unable to render main template");
        assert_eq!(result, source);
    }

    #[test]
    fn render_toc_test() {
        let source = include_str!("testdata/_toc.yaml");

        let mut table_of_contents_items: Vec<crate::TableOfContentsItem> = vec![];
        table_of_contents_items.push(crate::TableOfContentsItem {
            name: "fuchsia.auth".to_string(),
            link: "fuchsia.auth/README.md".to_string(),
            description: "Fuchsia Auth API".to_string(),
            added: "7".to_string(),
        });
        table_of_contents_items.push(crate::TableOfContentsItem {
            name: "fuchsia.bluetooth".to_string(),
            link: "fuchsia.bluetooth/README.md".to_string(),
            description: "Fuchsia Bluetooth API".to_string(),
            added: "HEAD".to_string(),
        });
        table_of_contents_items.push(crate::TableOfContentsItem {
            name: "fuchsia.media".to_string(),
            link: "fuchsia.media/README.md".to_string(),
            description: "Fuchsia Media API".to_string(),
            added: "".to_string(),
        });

        let table_of_contents = crate::TableOfContents {
            items: table_of_contents_items,
            versions: vec!["HEAD".to_string(), "7".to_string()],
            default_version: "7".to_string(),
        };

        let fidl_config = json!(null);
        let declarations: Vec<String> = Vec::new();

        let main_fidl_doc = json!({
            "table_of_contents": table_of_contents,
            "config": fidl_config,
            "search": declarations,
            "url_path": "/",
        });

        let pb = PathBuf::new();
        let template = MarkdownTemplate::new(&pb);

        let result = render_template(&template.handlebars, "toc".to_string(), &main_fidl_doc)
            .expect("Unable to render toc template");
        assert_eq!(result, source);
    }
}
