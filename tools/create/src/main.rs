// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

mod tmpl_helpers;
mod util;

use anyhow::{anyhow, bail};
use chrono::{Datelike, Utc};
use handlebars::Handlebars;
use serde_derive::Serialize;
use std::collections::HashMap;
use std::path::Path;
use std::str::FromStr;
use std::{env, fmt, fs, io};
use structopt::StructOpt;
use tempfile::tempdir;
use termion::{color, style};

fn main() -> Result<(), anyhow::Error> {
    let args = CreateArgs::from_args();
    if args.project_name.contains("_") {
        bail!("project-name cannot contain underscores");
    }
    let templates_dir_path = util::get_templates_dir_path()?;
    if !util::dir_contains(&templates_dir_path, &args.project_type)? {
        bail!("unrecognized project type \"{}\"", &args.project_type);
    }
    let project_template_path = templates_dir_path.join(&args.project_type);

    // Collect the template files for this project type and language.
    let templates = TemplateTree::from_dir(&project_template_path, &args.lang)?;

    // Create the set of variables accessible to template files.
    let tmpl_args = TemplateArgs::from_create_args(&args)?;

    // Register the template engine and execute the templates.
    let mut handlebars = Handlebars::new();
    handlebars.set_strict_mode(true);
    tmpl_helpers::register_helpers(&mut handlebars);
    let project = templates.render(&mut handlebars, &tmpl_args)?;

    // Write the rendered files to a temp directory.
    let dir = tempdir()?;
    let tmp_out_path = dir.path().join(&args.project_name);
    project.write(&tmp_out_path)?;

    // Rename the temp directory project to the final location.
    let dest_project_path = env::current_dir()?.join(&args.project_name);
    fs::rename(&tmp_out_path, &dest_project_path)?;

    println!("Project created at {}.", dest_project_path.to_string_lossy());

    // Find the parent BUILD.gn file and suggest adding the test target.
    let parent_build =
        dest_project_path.parent().map(|p| p.join("BUILD.gn")).filter(|b| b.exists());
    if let Some(parent_build) = parent_build {
        println!(
            "{}note:{} Don't forget to include the {}{}:tests{} GN target in the parent {}tests{} target ({}).",
            color::Fg(color::Yellow), color::Fg(color::Reset),
            style::Bold, &args.project_name, style::Reset,
            style::Bold, style::Reset,
            parent_build.to_string_lossy()
        );
    }

    Ok(())
}

#[derive(Debug, StructOpt)]
#[structopt(name = "fx-create", about = "Creates scaffolding for new projects.")]
struct CreateArgs {
    /// The type of project to create.
    ///
    /// This can be one of:
    ///
    /// - component-v2: A V2 component launched with Component Manager,
    #[structopt(name = "project-type")]
    project_type: String,

    /// The name of the new project.
    ///
    /// This will be the name of the GN target and directory for the project.
    /// The name should not contain any underscores.
    #[structopt(name = "project-name")]
    project_name: String,

    /// The programming language.
    #[structopt(short, long)]
    lang: Language,
}

/// Supported languages for project creation.
#[derive(Debug)]
enum Language {
    Rust,
    Cpp,
}

impl Language {
    /// Returns the language's template suffix. Template
    /// files that match this suffix belong to this language.
    fn template_suffix(&self) -> &'static str {
        match self {
            Self::Rust => ".tmpl-rust",
            Self::Cpp => ".tmpl-cpp",
        }
    }
}

impl FromStr for Language {
    type Err = String;

    fn from_str(s: &str) -> Result<Self, Self::Err> {
        Ok(match s {
            "rust" => Self::Rust,
            "cpp" => Self::Cpp,
            _ => return Err(format!("unrecognized language \"{}\"", s)),
        })
    }
}

impl fmt::Display for Language {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        f.write_str(match self {
            Self::Rust => "rust",
            Self::Cpp => "cpp",
        })
    }
}

/// The arguments passed in during template execution.
/// The fields defined here represent what variables can be present in template files.
/// Add a field here and populate it to make it accessible in a template.
///
/// NOTE: The fields are serialized to JSON when passed to templates. The serialization
/// process renames all fields to UPPERCASE.
#[derive(Debug, Serialize)]
#[serde(rename_all = "UPPERCASE")]
struct TemplateArgs {
    /// The current year, for use in copyright headers.
    /// Reference from a template with `{{COPYRIGHT_YEAR}}`.
    copyright_year: String,

    /// The project name, as given on the command line.
    /// Reference from a template with `{{PROJECT_NAME}}`.
    project_name: String,

    /// The path to the new project, relative to the FUCHSIA_DIR environment variable.
    /// Reference from a template with `{{PROJECT_PATH}}`.
    project_path: String,

    /// The project-type, as specified on the command line. E.g. 'component-v2'.
    project_type: String,
}

impl TemplateArgs {
    /// Build TemplateArgs from the program args and environment.
    fn from_create_args(create_args: &CreateArgs) -> Result<Self, anyhow::Error> {
        Ok(TemplateArgs {
            copyright_year: Utc::now().year().to_string(),
            project_name: create_args.project_name.clone(),
            project_path: {
                let absolute_project_path = env::current_dir()?.join(&create_args.project_name);
                let fuchsia_root = util::get_fuchsia_root()?;
                absolute_project_path
                    .strip_prefix(&fuchsia_root)
                    .map_err(|_| {
                        anyhow!(
                            "current working directory must be a descendant of FUCHSIA_DIR ({:?})",
                            &fuchsia_root
                        )
                    })?
                    .to_str()
                    .ok_or_else(|| anyhow!("invalid path {:?}", &absolute_project_path))?
                    .to_string()
            },
            project_type: create_args.project_type.clone(),
        })
    }
}

/// The in-memory filtered template file tree.
#[derive(Debug, PartialEq)]
enum TemplateTree {
    /// A file and its template contents.
    File(String),

    /// A directory and its entries.
    Dir(HashMap<String, Box<TemplateTree>>),
}

impl TemplateTree {
    /// Populate a TemplateTree recursively from a path, filtering by `lang`.
    /// See [`Language::template_suffix`].
    fn from_dir(path: &Path, lang: &Language) -> io::Result<Self> {
        Ok(if path.is_dir() {
            let mut templates = HashMap::new();
            for entry in fs::read_dir(path)? {
                let entry = entry?;
                let filename = match Self::filter_entry(&entry, lang)? {
                    Some(filename) => filename,
                    None => continue,
                };

                // Recursively create a TemplateTree from this subpath.
                let sub_templates = TemplateTree::from_dir(&entry.path(), lang)?;
                if !sub_templates.is_empty() {
                    templates.insert(filename, Box::new(sub_templates));
                }
            }
            TemplateTree::Dir(templates)
        } else {
            TemplateTree::File(fs::read_to_string(path)?)
        })
    }

    /// Filters a template directory entry based on language, returning the
    /// template filename without the `.tmpl` or `.tmpl-<lang>` extension.
    fn filter_entry(entry: &fs::DirEntry, lang: &Language) -> io::Result<Option<String>> {
        let filename = util::filename_to_string(entry.file_name())?;
        if entry.file_type()?.is_dir() {
            // Directories don't get filtered by language.
            Ok(Some(filename))
        } else {
            // Check if the file's extension matches the language-specific template
            // extension or the general template extension.
            let filter = util::strip_suffix(&filename, lang.template_suffix());
            let filter = filter.or_else(|| util::strip_suffix(&filename, ".tmpl"));
            Ok(filter.map(str::to_string))
        }
    }

    /// Checks if the file or directory is empty.
    fn is_empty(&self) -> bool {
        match self {
            Self::File(contents) => contents.is_empty(),
            Self::Dir(m) => m.is_empty(),
        }
    }

    /// Recursively renders this TemplateTree into a mirror type [`RenderedTree`],
    /// using `handlebars` as the template engine and `args` as the exported variables
    // accessible to the templates.
    fn render(
        &self,
        handlebars: &mut Handlebars,
        args: &TemplateArgs,
    ) -> Result<RenderedTree, handlebars::TemplateRenderError> {
        Ok(match self {
            Self::File(template_str) => {
                RenderedTree::File(handlebars.render_template(&template_str, args)?)
            }
            Self::Dir(nested_templates) => {
                let mut rendered_subtree = HashMap::new();
                for (filename, template) in nested_templates {
                    rendered_subtree.insert(
                        filename.replace("$", &args.project_name),
                        Box::new(template.render(handlebars, args)?),
                    );
                }
                RenderedTree::Dir(rendered_subtree)
            }
        })
    }
}

/// An in-memory representation of a file tree, where the paths and contents have
/// all been executed and rendered into their final form.
/// This is the mirror of [`TemplateTree`].
#[derive(Debug, PartialEq)]
enum RenderedTree {
    /// A file and its contents.
    File(String),

    /// A directory and its entries.
    Dir(HashMap<String, Box<RenderedTree>>),
}

impl RenderedTree {
    /// Write the RenderedTree to the `dest` path.
    fn write(&self, dest: &Path) -> io::Result<()> {
        match self {
            Self::File(contents) => {
                fs::write(dest, &contents)?;
            }
            Self::Dir(tree) => {
                fs::create_dir(dest)?;
                for (filename, subtree) in tree {
                    let dest = dest.join(filename);
                    subtree.write(&dest)?;
                }
            }
        }
        Ok(())
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn render_templates() {
        let templates = TemplateTree::Dir({
            let mut entries = HashMap::new();
            entries.insert(
                "file.h".to_string(),
                Box::new(TemplateTree::File("class {{PROJECT_NAME}};".to_string())),
            );
            entries.insert(
                "$_nested".to_string(),
                Box::new(TemplateTree::Dir({
                    let mut entries = HashMap::new();
                    entries.insert(
                        "file.h".to_string(),
                        Box::new(TemplateTree::File(
                            "#include \"{{PROJECT_PATH}}/file.h\"\n// `fx create {{PROJECT_TYPE}}`"
                                .to_string(),
                        )),
                    );
                    entries
                })),
            );
            entries
        });

        let mut handlebars = Handlebars::new();
        handlebars.set_strict_mode(true);
        let args = TemplateArgs {
            copyright_year: "2020".to_string(),
            project_name: "foo".to_string(),
            project_path: "bar/foo".to_string(),
            project_type: "component-v2".to_string(),
        };

        let rendered =
            templates.render(&mut handlebars, &args).expect("failed to render templates");
        assert_eq!(
            rendered,
            RenderedTree::Dir({
                let mut entries = HashMap::new();
                entries.insert(
                    "file.h".to_string(),
                    Box::new(RenderedTree::File("class foo;".to_string())),
                );
                entries.insert(
                    "foo_nested".to_string(),
                    Box::new(RenderedTree::Dir({
                        let mut entries = HashMap::new();
                        entries.insert(
                            "file.h".to_string(),
                            Box::new(RenderedTree::File(
                                "#include \"bar/foo/file.h\"\n// `fx create component-v2`"
                                    .to_string(),
                            )),
                        );
                        entries
                    })),
                );
                entries
            })
        );
    }
}
