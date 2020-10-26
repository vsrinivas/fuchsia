// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::{Context as _, Error};
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
            ("methodId", crate::templates::method_id),
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

    fn render_interface(&self, package: &str, fidl_json: &Value) -> Result<(), Error> {
        // Create a directory for interface files
        let output_dir = self.output_path.join(package);
        fs::create_dir(&output_dir)?;
        info!("Created output directory {}", output_dir.display());

        let package_index = output_dir.join("README.md");
        info!("Generating package documentation {}", package_index.display());

        let mut output_file = File::create(&package_index)
            .with_context(|| format!("Can't create {}", package_index.display()))?;

        // Render files
        let package_content =
            render_template(&self.handlebars, "interface".to_string(), &fidl_json)
                .with_context(|| format!("Can't render interface {}", package))?;
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
        .with_context(|| format!("Unable to render template '{}'", template_name))?;
    Ok(content)
}

#[cfg(test)]
mod test {
    use super::*;
    use glob::glob;
    use serde_json::json;
    use std::{env, fs};
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
    fn render_toc_test() {
        let source = include_str!("testdata/_toc.yaml");

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

        let result = render_template(&template.handlebars, "toc".to_string(), &main_fidl_doc)
            .expect("Unable to render toc template");
        assert_eq!(result, source);
    }

    #[test]
    fn golden_test() {
        let template = MarkdownTemplate::new(&PathBuf::new());

        // Load and parse fidldoc config file
        let fidl_config: Value =
            serde_json::from_str(include_str!("../../fidldoc.config.json")).unwrap();

        let goldens_path = goldens_path();
        let glob_pattern = goldens_path.join("*.json.golden");

        let mut num_goldens = 0;

        for entry in glob(&glob_pattern.to_str().unwrap()).unwrap() {
            if let Ok(ir_golden_path) = entry {
                num_goldens = num_goldens + 1;
                let golden = ir_golden_path.file_name().unwrap();
                let golden_name = golden.clone().to_str().unwrap();

                let ir_golden_data = fs::read_to_string(&ir_golden_path).unwrap();

                // For each golden JSON IR at <filename>.json.golden
                // there's a corresponding markdown at <filename>.json.golden.md
                let md_golden_path = goldens_path.join(format!("{}.md", golden_name));

                // Read and parse declarations from the JSON IR golden
                let mut declarations: Value = serde_json::from_str(&ir_golden_data)
                    .with_context(|| format!("Unable to parse testdata for {}", golden_name))
                    .unwrap();

                // Merge declarations with config file
                declarations["config"] = fidl_config.clone();
                declarations["tag"] = json!("HEAD");

                let result =
                    render_template(&template.handlebars, "interface".to_string(), &declarations)
                        .with_context(|| {
                            format!("Unable to render interface template for {}", golden_name)
                        })
                        .unwrap();

                // Set the environment variable REGENERATE_GOLDENS_FOLDER=<FOLDER>
                // when running tests to regenerate goldens in <FOLDER>
                match env::var("REGENERATE_GOLDENS_FOLDER") {
                    Ok(folder) => {
                        let mut path = PathBuf::from(folder);
                        // Create the destination folder if needed
                        fs::create_dir_all(&path).unwrap();
                        path.push(format!("{}.md", golden_name));
                        fs::write(path, result)
                            .with_context(|| {
                                format!("Unable to regenerate golden for {}", golden_name)
                            })
                            .unwrap();
                    }
                    Err(_) => {
                        let md_golden_data = fs::read_to_string(&md_golden_path)
                            .with_context(|| {
                                format!("Unable to read golden from {}", &md_golden_path.display())
                            })
                            .unwrap();
                        // Running regular test
                        assert_eq!(
                            result, md_golden_data,
                            "Generated output for template {} doesn't match goldenset",
                            golden_name
                        );
                    }
                }
            }
        }

        assert_ne!(num_goldens, 0, "Found 0 goldens in {}", &goldens_path.display());
    }

    fn goldens_path() -> PathBuf {
        let mut path = env::current_exe().unwrap();
        path.pop();
        path = path.join("test_data/fidldoc");
        path
    }
}
