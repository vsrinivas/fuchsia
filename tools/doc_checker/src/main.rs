// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! doc_checker is a CLI tool to check markdown files for correctness in
//! the Fuchsia project.

use {
    anyhow::{bail, Context, Result},
    argh::FromArgs,
    glob::glob,
    serde_yaml::Value,
    std::{
        fs::{self, File},
        io::BufReader,
        path::PathBuf,
    },
};

pub(crate) use crate::checker::{DocCheck, DocCheckError, DocLine, DocYamlCheck};
pub(crate) use crate::md_element::DocContext;
mod checker;
mod link_checker;
mod md_element;
mod parser;
mod yaml;

// path_helper includes methods to check path attributes
// so that these methods can be mocked for unit tests.
pub mod path_helper_module {

    use std::path::Path;
    pub fn exists(path: &Path) -> bool {
        path.exists()
    }
    pub fn is_dir(path: &Path) -> bool {
        path.is_dir()
    }
}

// Used when testing, the existence and is_dir
// is based on the name.
// Originally, This was attempted using mockall::automock.
// However, since the mocked module is global, and tests
// are run multithreaded, keeping the mocking context exclusive
// was hard. A mutex could be used, but mutex and async code
// do not work nicely together.
//
// As a result, a name based mock is used for all the tests.
#[cfg(test)]
pub(crate) mod mock_path_helper_module {
    use std::path::Path;
    pub fn exists(path: &Path) -> bool {
        let path_str = path.to_string_lossy();

        // If the path actually exists, return true. This allows for
        // staging test data.
        path.exists() ||

        // if is_dir returns true, the directory needs to exist as well.
        is_dir(path) ||

        // markdown files exist with a couple exceptions.
        path_str.ends_with(".md") && 
        (!path_str.ends_with("no_readme/README.md") &&
        !path_str.ends_with("unused/README.md") && 
        !path_str.ends_with("missing.md" ) )   ||

        // OWNERS file exists.
        path.ends_with("OWNERS")
    }

    pub fn is_dir(path: &Path) -> bool {
        // If the path is actually a directory, return true. This allows for
        // staged test data.
        path.is_dir() ||

        // Paths with no extension are directories, except OWNERS.
        ( path.extension().is_none() && !path.ends_with("OWNERS"))
    }
}

/// Check the markdown documentation using a variety of checks.
#[derive(Debug, FromArgs)]
pub struct DocCheckerArgs {
    /// path to the root of the checkout of the project.
    #[argh(option, default = r#"PathBuf::from(".")"#)]
    pub root: PathBuf,

    /// name of project to check, defaults to fuchsia.
    #[argh(option, default = r#"String::from("fuchsia")"#)]
    pub project: String,

    /// (Experimental) Name of the folder inside the project
    ///  which contains documents to check. Defaults to 'docs'.
    #[argh(option, default = r#"PathBuf::from("docs")"#)]
    pub docs_folder: PathBuf,

    /// do not resolve http(s) links
    #[argh(switch)]
    pub local_links_only: bool,
}

#[fuchsia::main]
async fn main() -> Result<()> {
    let mut opt: DocCheckerArgs = argh::from_env();

    // Canonicalize the root directory so the rest of the code can rely on
    // the root directory existing and being a normalized path.
    opt.root = opt
        .root
        .canonicalize()
        .context(format!("invalid root dir for source: {:?} ", &opt.root))?;

    if let Some(errors) = do_main(opt).await? {
        // Output the result
        for e in &errors {
            println!("{}: {}", e.doc_line, e.message);
        }
        bail!("{} errors found", errors.len())
    }
    Ok(())
}

/// The actual main function. It is refactored like this to make it easier
/// to run it in a unit test.
async fn do_main(opt: DocCheckerArgs) -> Result<Option<Vec<DocCheckError>>> {
    let root_dir = &opt.root;
    let docs_project = &opt.project;
    let docs_dir = root_dir.join(&opt.docs_folder);

    eprintln!("Checking Project {} {:?}.", docs_project, docs_dir);

    // Find all the markdown in the docs folder.
    let pattern = format!("{}/**/*.md", docs_dir.to_string_lossy());
    let markdown_files: Vec<PathBuf> = glob(&pattern)?
        // Keep only non-error results, mapping to Option<PathBuf>
        .filter_map(|p| p.ok())
        // Keep paths with file names, mapped to str&
        // and rop the hidden files that macs sometime make.
        .filter_map(|p| {
            if let Some(name) = p.file_name()?.to_str() {
                if !name.starts_with("._") {
                    Some(p)
                } else {
                    None
                }
            } else {
                None
            }
        })
        .collect();

    // Find all the .yaml files.
    let yaml_pattern = format!("{}/**/*.yaml", docs_dir.to_string_lossy());
    let yaml_files: Vec<PathBuf> = glob(&yaml_pattern)?.filter_map(|p| p.ok()).collect();

    eprintln!(
        "Checking {} markdown files and {} yaml files",
        markdown_files.len(),
        yaml_files.len()
    );

    /*
    Doc checking is broken into a couple major phases.

    1. Checks are registered from the modules that have structs that implement the DocCheck trait.
    2. Each markdown file is parsed into a stream of Elements. Each element is passed to each registered checker.
    3. After all the markdown files are parsed, the post-check check is called on each checker. This allows
       checkers to perform cross-file checks and checks that used data collected from the individual documents.
    4. Each yaml file is checked for each yaml checker registered.
    5. After all the yaml is checked, the post-check check is called.
    6. All the errors are collected and returned.
    */
    let mut markdown_checks: Vec<Box<dyn DocCheck>> = vec![];
    let mut errors: Vec<DocCheckError> = vec![];

    let checks = link_checker::register_markdown_checks(&opt)?;
    for c in checks {
        markdown_checks.push(c);
    }

    let mut yaml_checks = yaml::register_yaml_checks(&opt)?;

    let markdown_errors: Vec<DocCheckError> =
        check_markdown(&markdown_files, &mut markdown_checks)?;
    errors.extend(markdown_errors);

    let yaml_errors = check_yaml(&yaml_files, &mut yaml_checks)?;
    errors.extend(yaml_errors);

    // Post checks
    for c in yaml_checks {
        match c.post_check(&markdown_files, &yaml_files).await {
            Ok(Some(check_errors)) => errors.extend(check_errors),
            Ok(None) => {}
            Err(e) => errors.push(DocCheckError {
                doc_line: DocLine { line_num: 0, file_name: PathBuf::from("") },
                message: format!("Error {} running check: {} ", e, c.name()),
            }),
        }
    }
    for c in markdown_checks {
        match c.post_check().await {
            Ok(Some(check_errors)) => errors.extend(check_errors),
            Ok(None) => {}
            Err(e) => errors.push(DocCheckError {
                doc_line: DocLine { line_num: 0, file_name: PathBuf::from("") },
                message: format!("Error {} running check: {} ", e, c.name()),
            }),
        }
    }

    let result = if errors.is_empty() { None } else { Some(errors) };
    Ok(result)
}

/// Given the list of markdown files to check, iterate over each check, collecting any errors.
pub fn check_markdown<'a>(
    files: &[PathBuf],
    checks: &'a mut [Box<dyn DocCheck + 'static>],
) -> Result<Vec<DocCheckError>> {
    let mut errors: Vec<DocCheckError> = vec![];

    for mdfile in files {
        let mdcontent = fs::read_to_string(mdfile).expect("Unable to read file");
        let doc_context = DocContext::new(mdfile.clone(), &mdcontent);

        for element in doc_context {
            for c in &mut *checks {
                match c.check(&element) {
                    Ok(Some(check_errors)) => errors.extend(check_errors),
                    Ok(None) => {}
                    Err(e) => errors.push(DocCheckError {
                        doc_line: element.doc_line(),
                        message: format!("Error {} running check: {:?} ", e, c.name()),
                    }),
                }
            }
        }
    }
    Ok(errors)
}

fn check_yaml<'a>(
    yaml_files: &[PathBuf],
    checks: &'a mut [Box<dyn DocYamlCheck + 'static>],
) -> Result<Vec<DocCheckError>> {
    let mut errors: Vec<DocCheckError> = vec![];

    for yaml_file in yaml_files {
        let f = File::open(yaml_file)?;
        let val: Value = match serde_yaml::from_reader(BufReader::new(f)) {
            Ok(v) => v,
            Err(e) => bail!("Error parsing {:?}: {}", yaml_file, e),
        };
        for c in &mut *checks {
            match c.check(yaml_file, &val) {
                Ok(Some(check_errors)) => errors.extend(check_errors),
                Ok(None) => {}
                Err(e) => errors.push(DocCheckError {
                    doc_line: DocLine { line_num: 1, file_name: yaml_file.to_path_buf() },
                    message: format!("Error {} running check: {} ", e, c.name()),
                }),
            }
        }
    }

    Ok(errors)
}

#[cfg(test)]
mod test {

    use super::*;
    use std::env;

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_project_test() -> Result<()> {
        let opt = DocCheckerArgs {
            root: PathBuf::from("doc_checker_test_data"),
            project: "fuchsia".to_string(),
            docs_folder: PathBuf::from("docs"),
            local_links_only: true,
        };

        // Set the current directory to the executable dir so the relative test paths WAI.
        env::set_current_dir(env::current_exe()?.parent().unwrap_or(&PathBuf::from(".")))?;

        let expected: Vec<DocCheckError> = vec![
            DocCheckError::new(8, PathBuf::from("doc_checker_test_data/docs/README.md"),
                "in-tree link to /docs/missing.md could not be found at \"doc_checker_test_data/docs/missing.md\""),
            DocCheckError::new(11, PathBuf::from("doc_checker_test_data/docs/README.md"), 
                "Should not link to https://fuchsia.dev/fuchsia-src/path.md via https, use relative filepath"),
            DocCheckError::new(19, PathBuf::from("doc_checker_test_data/docs/README.md"),
                "Obsolete or invalid project garnet: https://fuchsia.googlesource.com/garnet/+/refs/heads/main/README.md"),
            DocCheckError::new(21,PathBuf::from("doc_checker_test_data/docs/README.md"),
                "Cannot normalize /docs/../../README.md, references parent beyond root."),
            DocCheckError::new(2,  PathBuf::from("doc_checker_test_data/docs/no_readme/details.md"),
                "in-tree link to /docs/no_readme could not be found at \"doc_checker_test_data/docs/no_readme\" or  \"doc_checker_test_data/docs/no_readme/README.md\""),
            DocCheckError::new(4,PathBuf::from("doc_checker_test_data/docs/path.md"),
                "in-tree link to /docs/missing-image.png could not be found at \"doc_checker_test_data/docs/missing-image.png\""),
            DocCheckError::new(4, PathBuf::from("doc_checker_test_data/docs/second.md"),
                "Invalid link http://{}.com/markdown : invalid uri character"),
            DocCheckError::new(1, PathBuf::from("doc_checker_test_data/docs/unused/_toc.yaml"),
                "in-tree link to /docs/unused could not be found at \"doc_checker_test_data/docs/unused\" or  \"doc_checker_test_data/docs/unused/README.md\""),
            DocCheckError::new(0, PathBuf::from("doc_checker_test_data/docs/unreachable.md"),
                "File not referenced in any _toc.yaml files."), 
            DocCheckError::new(0, PathBuf::from("doc_checker_test_data/docs/unused/_toc.yaml"),
                "File not reachable via _toc include references."),
        ];

        if let Some(actual_errors) = do_main(opt).await? {
            let mut expected_iter = expected.iter();
            for actual in actual_errors {
                if let Some(expected) = expected_iter.next() {
                    assert_eq!(&actual, expected);
                } else {
                    bail!("Unexpected error: {:?}", actual);
                }
            }
            let expected_errors: Vec<&DocCheckError> = expected_iter.collect();
            // Should be no other expected errors
            if !expected_errors.is_empty() {
                bail!("Expected at least one more error. Missing error(s): {:?}", expected_errors);
            }
        }
        Ok(())
    }
}
