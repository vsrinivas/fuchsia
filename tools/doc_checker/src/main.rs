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
#[cfg(test)]
use mockall::automock;

#[cfg_attr(test, automock)]
#[allow(dead_code)]
pub mod path_helper_module {

    use std::path::Path;
    pub fn exists(path: &Path) -> bool {
        path.exists()
    }
    pub fn is_dir(path: &Path) -> bool {
        path.is_dir()
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

fn main() -> Result<()> {
    let mut opt: DocCheckerArgs = argh::from_env();

    // Canonicalize the root directory so the rest of the code can rely on
    // the root directory existing and being a normalized path.
    opt.root = opt
        .root
        .canonicalize()
        .context(format!("invalid root dir for source: {:?} ", &opt.root))?;

    if let Some(errors) = do_main(opt)? {
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
fn do_main(opt: DocCheckerArgs) -> Result<Option<Vec<DocCheckError>>> {
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
        match c.post_check(&markdown_files, &yaml_files) {
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

    use {
        lazy_static::lazy_static,
        std::sync::{Mutex, MutexGuard},
    };

    // Since we are mocking global methods, we need to synchronize
    // the setting of the expectations on the mock. This is done using a Mutex.
    lazy_static! {
        pub static ref MTX: Mutex<()> = Mutex::new(());
    }

    // When a test panics, it will poison the Mutex. Since we don't actually
    // care about the state of the data we ignore that it is poisoned and grab
    // the lock regardless.  If you just do `let _m = &MTX.lock().unwrap()`, one
    // test panicking will cause all other tests that try and acquire a lock on
    // that Mutex to also panic.
    pub fn get_lock(m: &'static Mutex<()>) -> MutexGuard<'static, ()> {
        match m.lock() {
            Ok(guard) => guard,
            Err(poisoned) => poisoned.into_inner(),
        }
    }
}
