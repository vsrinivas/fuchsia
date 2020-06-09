// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

mod template_helpers;
mod util;

use anyhow::{anyhow, bail, Context};
use chrono::{Datelike, Utc};
use handlebars::Handlebars;
use serde::Serialize;
use std::collections::HashMap;
use std::path::{Component, Path, PathBuf};
use std::str::FromStr;
use std::{fmt, fs, io};
use structopt::StructOpt;
use termion::{color, style};

const LANG_AGNOSTIC_EXTENSION: &'static str = "tmpl";

fn main() -> Result<(), anyhow::Error> {
    let args = CreateArgs::from_args();
    let templates_dir_path = util::get_templates_dir_path()?;

    // Get the list of template file paths available to this project type and language.
    let template_files = find_template_files(
        &templates_dir_path.join("templates.json"),
        &args.project_type,
        &args.lang,
        &StdFs,
    )?;

    // Collect the template files for this project type and language.
    let template_tree = TemplateTree::from_file_list(
        &templates_dir_path,
        &template_files,
        &args.project_type,
        &StdFs,
    )?;

    // Register the template engine.
    let mut handlebars = Handlebars::new();
    handlebars.set_strict_mode(true);
    template_helpers::register_helpers(&mut handlebars);

    // Register partial templates.
    register_partial_templates(&mut handlebars, &templates_dir_path, &template_files, &StdFs)?;

    // Create the set of variables accessible to template files.
    let template_args = TemplateArgs::from_create_args(&args)?;

    // Execute the templates and render them to an in-memory representation.
    let project = template_tree.render(&mut handlebars, &template_args)?;

    // Ensure the destination doesn't exist.
    let dest_project_path = args.absolute_project_path()?;
    if dest_project_path.exists() {
        bail!("project directory already exists: {}", dest_project_path.display());
    }

    // Write the rendered files..
    project.write(&dest_project_path)?;

    if !args.silent {
        let project_name = &args.project_path.project_name;
        println!("Project created at {}.", dest_project_path.display());

        // Find the parent BUILD.gn file and suggest adding the test target.
        let parent_build =
            dest_project_path.parent().map(|p| p.join("BUILD.gn")).filter(|b| b.exists());
        if let Some(parent_build) = parent_build {
            println!(
                "{}note:{} Don't forget to include the {}{}:tests{} GN target in the parent {}tests{} target ({}).",
                color::Fg(color::Yellow), color::Fg(color::Reset),
                style::Bold, project_name, style::Reset,
                style::Bold, style::Reset,
                parent_build.display()
            );
        }
    }

    Ok(())
}

/// Creates scaffolding for new projects.
///
/// Eg.
///
/// fx create component-v1 src/sys/my-project --lang rust
#[derive(Debug, StructOpt)]
#[structopt(name = "fx-create", rename_all = "kebab")]
struct CreateArgs {
    /// The type of project to create.
    ///
    /// This can be one of:
    ///
    /// - component-v1: A V1 component launched with appmgr,
    ///
    /// - component-v2: A V2 component launched with Component Manager,
    ///
    /// - driver: A driver launched in a devhost,
    project_type: String,

    /// The path at which to create the new project.
    ///
    /// The last segment of the path will be the name of the GN target.
    /// GN-style paths are supported (//src/sys).
    /// The last segment of the path must start with an alphabetic
    /// character and be followed by zero or more alphanumeric characters,
    /// or the `-` character.
    project_path: ProjectPath,

    /// The programming language.
    #[structopt(short, long)]
    lang: Language,

    /// Override for the project include path. For testing.
    #[structopt(long)]
    override_project_path: Option<PathBuf>,

    /// Override the copyright year. For testing.
    #[structopt(long)]
    override_copyright_year: Option<u32>,

    /// When set, does not emit anything to stdout.
    #[structopt(long)]
    silent: bool,
}

/// A project path pointing to a non-existent leaf directory which
/// will be the home of the new project. The project name is derived
/// from the leaf directory.
#[derive(PartialEq, Eq, Debug, Clone)]
struct ProjectPath {
    project_parent: PathBuf,
    project_name: String,
}

impl FromStr for ProjectPath {
    type Err = anyhow::Error;

    fn from_str(s: &str) -> Result<Self, Self::Err> {
        // First handle a GN absolute path (//foo/bar).
        let path = if s.starts_with("//") {
            util::get_fuchsia_root()?.join(Path::new(&s[2..]))
        } else {
            Path::new(s).to_path_buf()
        };
        let project_parent = {
            let mut parent = path.parent().expect("parent should never be None");
            if parent == Path::new("") {
                parent = Path::new(".");
            }
            parent.canonicalize().context("path to project is invalid")?
        };
        let project_name =
            util::filename_to_string(path.file_name().with_context(|| "missing project name")?)?;
        let mut chars_iter = project_name.chars();
        if !chars_iter.next().with_context(|| "project name is empty")?.is_alphabetic() {
            return Err(anyhow!("project name must start with an alphabetic character"));
        }
        if chars_iter.any(|c| !c.is_alphanumeric() && c != '-') {
            return Err(anyhow!("project name must only contain alphanumeric characters and `-`"));
        }
        Ok(ProjectPath { project_parent, project_name })
    }
}

impl CreateArgs {
    /// Returns the absolute path to the new project.
    fn absolute_project_path(&self) -> io::Result<PathBuf> {
        Ok(self.project_path.project_parent.join(&self.project_path.project_name))
    }
}

/// Supported languages for project creation.
#[derive(Debug)]
enum Language {
    Rust,
    Cpp,
}

impl Language {
    /// Returns the language's template extension. Template
    /// files that match this extension belong to this language.
    fn template_extension(&self) -> &'static str {
        match self {
            Self::Rust => "tmpl-rust",
            Self::Cpp => "tmpl-cpp",
        }
    }

    // Check if the file's extension matches the language-specific template
    // extension or the general template extension.
    fn matches(&self, path: &Path) -> bool {
        if let Some(ext) = path.extension() {
            ext == self.template_extension() || ext == LANG_AGNOSTIC_EXTENSION
        } else {
            false
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

/// Given a path to a JSON file, extract a list of template files that match the
/// `project_type` and `lang`.
///
/// The JSON file must consist of a root list of strings, where each string is a path relative
/// to the directory that contains the JSON file.
///
/// E.g.
///
/// `out/default/host-tools/create_templates/templates.json`:
/// ```json
/// [
///     "component-v2/BUILD.gn.tmpl-rust",
///     "component-v2/src/main.rs.tmpl-rust"
/// ]
/// ```
///
/// This file is generated by the GN build rule in //tools/create/templates/BUILD.gn.
fn find_template_files<FR>(
    json_file: &Path,
    project_type: &str,
    lang: &Language,
    file_reader: &FR,
) -> Result<Vec<PathBuf>, anyhow::Error>
where
    FR: FileReader,
{
    let json_contents = file_reader.read_to_string(json_file)?;
    let template_files: Vec<PathBuf> = serde_json::from_slice(json_contents.as_bytes())?;
    let mut template_files: Vec<PathBuf> = template_files
        .into_iter()
        .filter(|p| {
            lang.matches(p)
                && (p.starts_with(project_type) || (is_partial_template(p) && is_root_file(p)))
        })
        .collect();
    template_files.sort();
    Ok(template_files)
}

/// Returns true if this path is a single segment path, e.g. "foo.txt".
fn is_root_file(path: &Path) -> bool {
    let mut iter = path.components();
    match iter.next() {
        Some(Component::Normal(_)) => iter.next().is_none(),
        _ => false,
    }
}

/// Returns true if the template file name begins with an `_`.
fn is_partial_template(path: impl AsRef<Path>) -> bool {
    path.as_ref()
        .file_name()
        .and_then(std::ffi::OsStr::to_str)
        .map(|f| f.starts_with('_'))
        .unwrap_or(false)
}

/// Registers any partial templates (templates whose file names begin with `_`) with the templating
/// engine.
fn register_partial_templates<FR>(
    handlebars: &mut Handlebars,
    templates_dir: &Path,
    template_files: &Vec<PathBuf>,
    file_reader: &FR,
) -> Result<(), anyhow::Error>
where
    FR: FileReader,
{
    for partial in template_files.into_iter().filter(|p| is_partial_template(p)) {
        let content = file_reader.read_to_string(templates_dir.join(partial))?;
        let partial = partial.with_extension("");

        // Unwrap is safe as the `is_partial_template` predicate expects a valid filename.
        let filename = util::filename_to_string(partial.file_name().unwrap())?;

        // `is_partial_template` guarantees this filename starts with a `_`.
        let partial_name = partial.with_file_name(&filename[1..]);

        handlebars.register_partial(&partial_name.display().to_string(), content)?;
    }
    Ok(())
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
        let project_path = if let Some(override_project_path) = &create_args.override_project_path {
            override_project_path.join(&create_args.project_path.project_name)
        } else {
            let absolute_project_path = create_args.absolute_project_path()?;
            let fuchsia_root = util::get_fuchsia_root()?;
            absolute_project_path
                .strip_prefix(&fuchsia_root)
                .map_err(|_| {
                    anyhow!(
                        "current working directory ({:?}) must be a descendant of FUCHSIA_DIR ({:?})", &absolute_project_path,
                        &fuchsia_root
                    )
                })?
                .to_path_buf()
        };

        let copyright_year = if let Some(year) = &create_args.override_copyright_year {
            year.to_string()
        } else {
            Utc::now().year().to_string()
        };

        Ok(TemplateArgs {
            copyright_year,
            project_name: create_args.project_path.project_name.clone(),
            project_path: project_path
                .to_str()
                .ok_or_else(|| anyhow!("invalid path {:?}", &project_path))?
                .to_string(),
            project_type: create_args.project_type.clone(),
        })
    }
}

/// The in-memory filtered template file tree.
#[derive(Debug, PartialEq)]
enum TemplateTree {
    /// A file and its template contents.
    File { source: PathBuf, content: String },

    /// A directory and its entries.
    Dir(HashMap<String, Box<TemplateTree>>),
}

impl TemplateTree {
    /// Populate a TemplateTree from a set of template files, ignoring partial templates (template files
    /// whose names begin with `_`).
    fn from_file_list<FR>(
        template_dir: &Path,
        template_files: &Vec<PathBuf>,
        project_type: &str,
        file_reader: &FR,
    ) -> Result<Self, anyhow::Error>
    where
        FR: FileReader,
    {
        let template_files = template_files
            .into_iter()
            .filter(|p| !is_partial_template(p))
            .collect::<Vec<&PathBuf>>();
        if template_files.is_empty() {
            bail!("no templates found for project type \"{}\"", project_type);
        }
        let mut tree = TemplateTree::Dir(HashMap::new());
        for path in template_files {
            let content = file_reader.read_to_string(template_dir.join(path))?;

            // The original template source for error reporting.
            let source = Path::new("//tools/create/templates").join(path);

            // Strip the project prefix from the path, which we know is there.
            let path = path.strip_prefix(project_type).unwrap();

            // Strip the .tmpl* extension. This will uncover the intended extension.
            // Eg: foo.rs.tmpl-rust -> foo.rs
            let path = path.with_extension("");

            tree.insert(&path, source, content)?;
        }
        Ok(tree)
    }

    fn insert(
        &mut self,
        path: &Path,
        source: PathBuf,
        content: String,
    ) -> Result<(), anyhow::Error> {
        let subtree = match self {
            Self::Dir(ref mut subtree) => subtree,
            Self::File { .. } => bail!("cannot insert subtree into file"),
        };

        let mut path_iter = path.components();
        if let Some(Component::Normal(component)) = path_iter.next() {
            let name = util::filename_to_string(component)?;
            let rest = path_iter.as_path();
            if path_iter.next().is_some() {
                subtree
                    .entry(name)
                    .or_insert_with(|| Box::new(TemplateTree::Dir(HashMap::new())))
                    .insert(rest, source, content)?;
            } else {
                if subtree.insert(name, Box::new(TemplateTree::File { source, content })).is_some()
                {
                    bail!("duplicate paths");
                }
            }
            Ok(())
        } else {
            bail!("path must be relative and have no '..' or '.' components");
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
            Self::File { source, content } => {
                let template_name = source.display().to_string();
                handlebars.register_template_string(&template_name, &content)?;
                let args = TemplateArgsWithTemplatePath { args, template_path: &template_name };
                let result = RenderedTree::File(handlebars.render(&template_name, &args)?);
                handlebars.unregister_template(&template_name);
                result
            }
            Self::Dir(nested_templates) => {
                let mut rendered_subtree = HashMap::new();
                for (filename, template) in nested_templates {
                    rendered_subtree.insert(
                        handlebars.render_template(&filename, args)?,
                        Box::new(template.render(handlebars, args)?),
                    );
                }
                RenderedTree::Dir(rendered_subtree)
            }
        })
    }
}

#[derive(Serialize)]
#[serde(rename_all = "UPPERCASE")]
struct TemplateArgsWithTemplatePath<'a> {
    #[serde(flatten)]
    args: &'a TemplateArgs,
    template_path: &'a str,
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
    fn write(&self, dest: &Path) -> Result<(), anyhow::Error> {
        match self {
            Self::File(contents) => {
                fs::write(dest, &contents)
                    .with_context(|| format!("failed to write to {:?}", dest))?;
            }
            Self::Dir(tree) => {
                fs::create_dir(dest)
                    .with_context(|| format!("failed to create directory {:?}", dest))?;
                for (filename, subtree) in tree {
                    let dest = dest.join(filename);
                    subtree.write(&dest)?;
                }
            }
        }
        Ok(())
    }
}

/// Trait to enable testing of the template tree creation logic.
/// Allows mocking out reading from the file system.
trait FileReader {
    fn read_to_string(&self, p: impl AsRef<Path>) -> Result<String, anyhow::Error>;
}

/// Standard library filesystem implementation of FileReader.
struct StdFs;

impl FileReader for StdFs {
    fn read_to_string(&self, p: impl AsRef<Path>) -> Result<String, anyhow::Error> {
        let p = p.as_ref();
        fs::read_to_string(p).with_context(|| format!("failed to read from {:?}", p))
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use matches::assert_matches;
    use std::env;
    use tempfile::tempdir;

    #[test]
    fn project_path_parsed() {
        // Create a dir structure like //foo, where FUCHSIA_DIR points to //.
        let temp_dir = tempdir().expect("failed to create temp dir");
        let foo_path = temp_dir.path().join("foo");
        std::fs::File::create(&foo_path).expect("failed to create foo dir");

        // Test GN absolute labels (need to point to actual file, so set FUCHSIA_DIR to temp dir).
        env::set_var("FUCHSIA_DIR", temp_dir.path());
        assert_eq!(
            "//foo/bar".parse::<ProjectPath>().unwrap(),
            ProjectPath { project_parent: foo_path.clone(), project_name: "bar".to_string() }
        );
        assert_eq!(
            "//bar".parse::<ProjectPath>().unwrap(),
            ProjectPath {
                project_parent: temp_dir.path().to_path_buf(),
                project_name: "bar".to_string()
            }
        );

        // Test absolute paths (need to point to actual files, so use the temp dir).
        assert_eq!(
            foo_path.join("bar").display().to_string().parse::<ProjectPath>().unwrap(),
            ProjectPath { project_parent: foo_path.clone(), project_name: "bar".to_string() }
        );
        assert_eq!(
            temp_dir.path().join("bar").display().to_string().parse::<ProjectPath>().unwrap(),
            ProjectPath {
                project_parent: temp_dir.path().to_path_buf(),
                project_name: "bar".to_string()
            }
        );

        // Test relative paths (need to point to actual files, so change current dir to temp dir).
        with_current_dir(temp_dir.path(), || {
            assert_eq!(
                "foo/bar".parse::<ProjectPath>().unwrap(),
                ProjectPath { project_parent: foo_path.clone(), project_name: "bar".to_string() }
            );
            assert_eq!(
                "bar".parse::<ProjectPath>().unwrap(),
                ProjectPath {
                    project_parent: temp_dir.path().to_path_buf(),
                    project_name: "bar".to_string()
                }
            );
        });
    }

    /// Runs a closure with the current working directory set to `path`. When the closure finishes
    /// executing, the previous working directory is reinstated.
    fn with_current_dir<P, F, R>(path: P, f: F) -> R
    where
        P: AsRef<Path>,
        F: FnOnce() -> R + std::panic::UnwindSafe,
    {
        let prev_dir = env::current_dir().expect("failed to get current dir");
        env::set_current_dir(path).expect("failed to set current dir");
        let result = std::panic::catch_unwind(f);
        env::set_current_dir(prev_dir).expect("failed to restore previous current dir");
        match result {
            Ok(r) => r,
            Err(err) => std::panic::resume_unwind(err),
        }
    }

    #[test]
    fn project_name_is_valid() {
        // Set FUCHSIA_DIR to something valid.
        env::set_var("FUCHSIA_DIR", env::current_dir().expect("failed to get current dir"));

        assert_matches!("foo_bar".parse::<ProjectPath>(), Err(_));
        assert_matches!("foo.bar".parse::<ProjectPath>(), Err(_));
        assert_matches!("1foo".parse::<ProjectPath>(), Err(_));
        assert_matches!("-foo".parse::<ProjectPath>(), Err(_));

        assert_matches!("foo-bar".parse::<ProjectPath>(), Ok(_));
        assert_matches!("foo1".parse::<ProjectPath>(), Ok(_));
    }

    #[test]
    fn language_display_can_be_parsed() {
        assert_matches!(Language::Rust.to_string().parse(), Ok(Language::Rust));
        assert_matches!(Language::Cpp.to_string().parse(), Ok(Language::Cpp));
    }

    #[test]
    fn render_templates() {
        let templates = TemplateTree::Dir({
            let mut entries = HashMap::new();
            entries.insert(
                "file.h".to_string(),
                Box::new(TemplateTree::File {
                    source: PathBuf::from("//file.h.tmpl-cpp"),
                    content: "class {{PROJECT_NAME}};".to_string(),
                }),
            );
            entries.insert(
                "{{PROJECT_NAME}}_nested".to_string(),
                Box::new(TemplateTree::Dir({
                    let mut entries = HashMap::new();
                    entries.insert(
                        "file.h".to_string(),
                        Box::new(TemplateTree::File{
                            source: PathBuf::from("//{{PROJECT_NAME}}_nested/file.h.tmpl-cpp"),
                            content: "#include \"{{PROJECT_PATH}}/file.h\"\n// `fx create {{PROJECT_TYPE}}`"
                                .to_string(),
                        }),
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

    impl<'a> FileReader for HashMap<&'a Path, &'a str> {
        fn read_to_string(&self, path: impl AsRef<Path>) -> Result<String, anyhow::Error> {
            self.get(path.as_ref()).map(|c| c.to_string()).ok_or_else(|| {
                anyhow::Error::from(io::Error::new(io::ErrorKind::NotFound, "file not found"))
            })
        }
    }

    #[test]
    fn template_tree_from_file_list() {
        let mut files = HashMap::new();
        files.insert(
            Path::new("create-templates/component-v2/main.cc.tmpl-cpp"),
            r#"{{PROJECT_NAME}}"#,
        );
        let tree = TemplateTree::from_file_list(
            Path::new("create-templates"),
            &vec![PathBuf::from("component-v2/main.cc.tmpl-cpp")],
            "component-v2",
            &files,
        )
        .expect("failed");
        assert_matches!(tree, TemplateTree::Dir(nested) if nested.contains_key("main.cc"));
    }

    #[test]
    fn test_find_template_files() {
        let mut files = HashMap::new();
        files.insert(
            Path::new("create-templates/templates.json"),
            r#"[
                "_partial.tmpl",
                "component-v2/_partial.tmpl",
                "component-v2/src/main.rs.tmpl-rust",
                "component-v1/_partial.tmpl"
            ]"#,
        );
        files.insert(Path::new("create-templates/_partial.tmpl"), r#"root {{PROJECT_NAME}}"#);
        files.insert(
            Path::new("create-templates/component-v2/_partial.tmpl"),
            r#"component-v2 {{PROJECT_NAME}}"#,
        );
        files.insert(
            Path::new("create-templates/component-v2/src/main.rs.tmpl-rust"),
            r#"component-v2 {{PROJECT_NAME}}"#,
        );
        files.insert(
            Path::new("create-templates/component-v1/_partial.tmpl"),
            r#"component-v1 {{PROJECT_NAME}}"#,
        );
        let mut files = find_template_files(
            Path::new("create-templates/templates.json"),
            "component-v2",
            &Language::Rust,
            &files,
        )
        .expect("failed");

        files.sort();
        assert_eq!(
            &files,
            &[
                PathBuf::from("_partial.tmpl"),
                PathBuf::from("component-v2/_partial.tmpl"),
                PathBuf::from("component-v2/src/main.rs.tmpl-rust"),
            ],
        );
    }

    #[test]
    fn test_register_partial_templates() {
        let mut files = HashMap::new();
        files.insert(Path::new("create-templates/_partial.tmpl"), r#"root {{PROJECT_NAME}}"#);
        files.insert(
            Path::new("create-templates/component-v2/_partial.tmpl"),
            r#"component-v2 {{PROJECT_NAME}}"#,
        );
        let file_list = vec![
            PathBuf::from("_partial.tmpl"),
            PathBuf::from("component-v2/_partial.tmpl"),
            PathBuf::from("component-v2/src/main.rs.tmpl-rust"),
        ];
        let mut handlebars = Handlebars::new();
        register_partial_templates(
            &mut handlebars,
            Path::new("create-templates"),
            &file_list,
            &files,
        )
        .expect("failed");
        let registered_templates = handlebars.get_templates();
        assert!(registered_templates.contains_key("partial"));
        assert!(registered_templates.contains_key("component-v2/partial"));
        assert_eq!(registered_templates.len(), 2);

        let mut args = HashMap::new();
        args.insert("PROJECT_NAME", "foo");
        assert_eq!(
            handlebars.render_template("{{>partial}}", &args).expect("failed to render"),
            "root foo"
        );
        assert_eq!(
            handlebars
                .render_template("{{>component-v2/partial}}", &args)
                .expect("failed to render"),
            "component-v2 foo"
        );
    }
}
