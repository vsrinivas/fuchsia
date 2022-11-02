// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! yaml checks the yaml  files that part of the //docs publishing process
//! for correctness.

use {
    self::toc_checker::Toc,
    crate::{
        link_checker::{
            do_check_link, do_in_tree_check, is_intree_link, LinkReference, PUBLISHED_DOCS_HOST,
        },
        DocCheckError, DocCheckerArgs, DocLine, DocYamlCheck,
    },
    anyhow::Result,
    serde::{de::DeserializeOwned, Deserialize},
    serde_yaml::{Mapping, Value},
    std::{
        collections::HashSet,
        ffi::OsStr,
        path::{self, Path, PathBuf},
    },
};

mod toc_checker;

cfg_if::cfg_if! {
    if #[cfg(test)] {
        use crate::mock_path_helper_module as path_helper;
    } else {
       use crate::path_helper_module as path_helper;
    }
}

#[derive(Deserialize, PartialEq, Debug)]
struct AreaEntry {
    name: String,
    api_primary: String,
    api_secondary: String,
    description: Option<String>,
    examples: Option<Vec<Mapping>>,
}

#[derive(Deserialize, PartialEq, Debug)]
struct Deprecations {
    included: Vec<FromTo>,
}

#[derive(Deserialize, Debug)]
// Dead code is used here so the names
// of the fields can be used by Deserialize
// even though there is no reading of the fields.
#[allow(dead_code)]
struct DriverEpitaph {
    short_description: String,
    deletion_reason: String,
    gerrit_change_id: String,
    available_in_git: String,
    areas: Option<Vec<String>>,
    path: String,
}

#[derive(Deserialize, PartialEq, Debug)]
struct EngCouncil {
    members: Vec<String>,
}

#[derive(Deserialize, Eq, PartialEq, Debug)]
pub struct FromTo {
    pub from: String,
    pub to: String,
}

#[derive(Deserialize, Debug)]
// Dead code is used here so the names
// of the fields can be used by Deserialize
// even though there is no reading of the fields.
#[allow(dead_code)]
struct GlossaryTerm {
    term: String,
    short_description: String,
    full_description: Option<String>,
    see_also: Option<Vec<String>>,
    related_guides: Vec<String>,
    area: Vec<String>,
}

#[derive(Deserialize, Debug)]
// Dead code is used here so the names
// of the fields can be used by Deserialize
// even though there is no reading of the fields.
#[allow(dead_code)]
struct GuideEntry {
    #[serde(alias = "type")]
    entry_type: String,
    product: String,
    board: String,
    method: String,
    host: String,
    url: String,
    title: String,
}

#[derive(Deserialize, Debug)]
// Dead code is used here so the names
// of the fields can be used by Deserialize
// even though there is no reading of the fields.
#[allow(dead_code)]
struct Metadata {
    descriptions: Mapping,
    columns: Vec<String>,
    types: Vec<String>,
    products: Vec<String>,
    boards: Vec<String>,
    methods: Vec<String>,
    hosts: Vec<String>,
    guides: Vec<GuideEntry>,
}

#[derive(Deserialize, Debug)]
// Dead code is used here so the names
// of the fields can be used by Deserialize
// even though there is no reading of the fields.
#[allow(dead_code)]
struct ProblemEntry {
    key: String,
    use_case: String,
    description: String,
    #[serde(alias = "related-problems")]
    related_problems: Vec<String>,
}

#[derive(Deserialize, PartialEq, Debug)]
struct Redirects {
    redirects: Vec<FromTo>,
}

#[derive(Deserialize, PartialEq, Debug)]
struct RfcEntry {
    name: String,
    title: String,
    short_description: String,
    authors: Vec<String>,
    file: String,
    area: Vec<String>,
    issue: Vec<String>,
    gerrit_change_id: Vec<String>,
    status: String,
    reviewers: Vec<String>,
    submitted: String,
    reviewed: String,
}

#[derive(Deserialize, PartialEq, Debug)]
struct RoadmapEntry {
    workstream: String,
    area: String,
    category: Vec<String>,
}

#[derive(Deserialize, Debug)]
// Dead code is used here so the names
// of the fields can be used by Deserialize
// even though there is no reading of the fields.
#[allow(dead_code)]
struct SysConfigEntry {
    name: String,
    description: String,
    architecture: String,
    #[serde(alias = "RAM")]
    ram: Option<String>,
    storage: Option<String>,
    manufacturer_link: Option<String>,
    board_driver_location: String,
}

#[derive(Deserialize, Debug)]
// Dead code is used here so the names
// of the fields can be used by Deserialize
// even though there is no reading of the fields.
#[allow(dead_code)]
struct ToolsEntry {
    name: String,
    team: String,
    links: Mapping,
    description: String,
    related: Option<Vec<String>>,
}

#[derive(Debug)]
pub(crate) struct YamlChecker {
    root_dir: PathBuf,
    docs_folder: PathBuf,
    project: String,
    check_external_links: bool,
}

impl DocYamlCheck for YamlChecker {
    fn name(&self) -> &str {
        "DocYamlCheck"
    }

    fn check<'a>(
        &mut self,
        filename: &Path,
        yaml_value: &serde_yaml::Value,
    ) -> Result<Option<Vec<DocCheckError>>> {
        if let Some(yaml_name) = filename.file_name() {
            let result = match yaml_name.to_str() {
                Some("_areas.yaml") => check_areas(filename, yaml_value),
                Some("_deprecated-docs.yaml") => check_deprecated_docs(filename, yaml_value),
                Some("_drivers_areas.yaml") => check_drivers_areas(filename, yaml_value),
                Some("_drivers_epitaphs.yaml") => check_drivers_epitaphs(filename, yaml_value),
                Some("_eng_council.yaml") => check_eng_council(filename, yaml_value),
                Some("_glossary.yaml") => check_glossary(filename, yaml_value),
                Some("_metadata.yaml") => check_metadata(filename, yaml_value),
                Some("_problems.yaml") => check_problems(filename, yaml_value),
                Some("_redirects.yaml") => check_redirects(filename, yaml_value),
                Some("_rfcs.yaml") => check_rfcs(filename, yaml_value),
                Some("_roadmap.yaml") => check_roadmap(filename, yaml_value),
                Some("_supported_cpu_architecture.yaml") => {
                    check_supported_cpu_architecture(filename, yaml_value)
                }
                Some("_supported_sys_config.yaml") => {
                    check_supported_sys_config(filename, yaml_value)
                }
                Some("_toc.yaml") => toc_checker::check_toc(
                    &self.root_dir,
                    &self.docs_folder,
                    &self.project,
                    filename,
                    yaml_value,
                ),
                Some("_tools.yaml") => check_tools(filename, yaml_value),
                Some(name) => todo!("Need to handle {} ({:?})", name, filename),
                _ => panic!("No str avail for {:?}", filename),
            };
            Ok(result)
        } else {
            Ok(None)
        }
    }

    fn post_check(
        &self,
        _markdown_files: &[PathBuf],
        _yaml_files: &[PathBuf],
    ) -> Result<Option<Vec<DocCheckError>>> {
        let mut yaml_file_set: HashSet<&PathBuf> = HashSet::from_iter(_yaml_files.iter());
        let mut visited: HashSet<PathBuf> = HashSet::new();
        let mut markdown_file_set: HashSet<&PathBuf> = HashSet::from_iter(_markdown_files.iter());
        let mut errors = vec![];
        let mut external_links = vec![];

        // Some special paths that are not in the //docs dir that need to be added
        let code_of_conduct_md = self.root_dir.join("CODE_OF_CONDUCT.md");
        markdown_file_set.insert(&code_of_conduct_md);
        let contrib_md = self.root_dir.join("CONTRIBUTING.md");
        markdown_file_set.insert(&contrib_md);

        // Start with //docs/_toc.yaml
        let mut toc_stack = vec![self.root_dir.join("docs/_toc.yaml")];
        while let Some(current_yaml) = toc_stack.pop() {
            if let Some(yaml_doc) = yaml_file_set.take(&current_yaml) {
                visited.insert(yaml_doc.clone());
                let toc = Toc::from(yaml_doc)?;
                // remove paths to markdown
                if let Some(path_list) = toc.get_paths() {
                    for p in path_list {
                        if is_external_path(&p) {
                            if self.check_external_links {
                                if p.starts_with("/reference") {
                                    external_links.push(LinkReference {
                                        link: format!("https://{}{}", PUBLISHED_DOCS_HOST, p),
                                        location: DocLine {
                                            line_num: 0,
                                            file_name: current_yaml.clone(),
                                        },
                                    });
                                } else if p.starts_with("https://") || p.starts_with("http://") {
                                    external_links.push(LinkReference {
                                        link: p.to_string(),
                                        location: DocLine {
                                            line_num: 0,
                                            file_name: current_yaml.clone(),
                                        },
                                    });
                                } else if p.starts_with("//") {
                                    external_links.push(LinkReference {
                                        link: format!("https:{}", p),
                                        location: DocLine {
                                            line_num: 0,
                                            file_name: current_yaml.clone(),
                                        },
                                    });
                                }
                            }
                            continue;
                        } else {
                            let rel_path = p.strip_prefix('/').unwrap_or(p.as_str());
                            let mut file_path = self.root_dir.join(rel_path);
                            if path_helper::is_dir(&file_path) {
                                file_path.push("README.md");
                            }

                            if markdown_file_set.take(&file_path).is_none()
                                && !visited.contains(&file_path)
                            {
                                errors.push(DocCheckError {
                                    doc_line: DocLine { line_num: 0, file_name: yaml_doc.clone() },
                                    message: format!("Reference to missing file: {}", p),
                                });
                            } else {
                                visited.insert(file_path);
                            }
                        }
                    }
                }
                // follow include
                if let Some(includes) = toc.get_includes() {
                    // All includes are /docs/... so just append the root.
                    let additional_paths = includes
                        .iter()
                        .map(|p| self.root_dir.join(p.strip_prefix('/').unwrap_or(p.as_str())));
                    toc_stack.extend(additional_paths);
                }
            } else if !visited.contains(&current_yaml) {
                return Ok(Some(vec![DocCheckError {
                    doc_line: DocLine { line_num: 0, file_name: current_yaml.clone() },
                    message: format!("Cannot find {:?} at {:?}", &current_yaml, &yaml_file_set),
                }]));
            }
        }

        markdown_file_set
            .iter()
            .filter(|f| **f != &code_of_conduct_md && **f != &contrib_md)
            .filter(|p| {
                if let Some(name) = p.file_name() {
                    name != "navbar.md" && !name.to_str().unwrap_or_default().starts_with('_')
                } else {
                    false
                }
            })
            .filter(|p| {
                !p.components().any(|c| c == path::Component::Normal(OsStr::new("_common")))
            })
            .filter(|p| !p.ends_with("gen/build_arguments.md"))
            .copied()
            .for_each(|f| {
                errors.push(DocCheckError {
                    doc_line: DocLine { line_num: 0, file_name: f.clone() },
                    message: "File not referenced in any _toc.yaml files.".to_string(),
                });
            });

        yaml_file_set.iter().filter(|f| f.ends_with("_toc.yaml")).for_each(|&f| {
            errors.push(DocCheckError {
                doc_line: DocLine { line_num: 0, file_name: f.clone() },
                message: "File not reachable via _toc include references.".to_string(),
            })
        });

        if self.check_external_links {
            /* Coming in next CL
            if let Some(link_errors) = check_external_links(&external_links).await {
                for e in link_errors {
                    errors.push(e);
                }
            }
            */
        }

        if errors.is_empty() {
            Ok(None)
        } else {
            Ok(Some(errors))
        }
    }
}

fn is_external_path(p: &str) -> bool {
    // treat reference docs as external
    p.starts_with("/reference")
        || p.starts_with("https://")
        || p.starts_with("http://")
        || p.starts_with("//")
}

/// Checks the path property from a yaml file.
fn check_path(
    doc_line: &DocLine,
    root_path: &Path,
    docs_folder: &Path,
    project: &str,
    path: &str,
) -> Option<DocCheckError> {
    let root_dir = root_path.display().to_string();
    match do_check_link(doc_line, path, project) {
        Ok(Some(doc_error)) => return Some(doc_error),
        Err(e) => {
            return Some(DocCheckError { doc_line: doc_line.clone(), message: format!("{}", e) })
        }
        Ok(None) => {}
    };

    // These files are in the root of the project, not in the docs directory, so they need special
    // treatment.
    if ["/CONTRIBUTING.md", "/CODE_OF_CONDUCT.md"].contains(&path) {
        let filepath = root_path.join(path.strip_prefix('/').unwrap_or(path));
        if !path_helper::exists(&filepath) {
            return Some(DocCheckError {
                doc_line: doc_line.clone(),
                message: format!("File: {:?} not found.", filepath),
            });
        }
        return None;
    }

    match is_intree_link(project, &root_dir, docs_folder, path) {
        Ok(Some(in_tree_path)) => {
            // Handle in-tree paths that are not in the docs_folder.
            // Since this is a table of contents, all the entries need
            // to be to the docs_folder, except /reference, which is a special case.
            if !in_tree_path.starts_with(PathBuf::from("/").join(docs_folder)) {
                if in_tree_path.starts_with("/reference") {
                    None
                } else {
                    Some(DocCheckError {
                        doc_line: doc_line.clone(),
                        message: format!(
                            "invalid path {}. Path must be in /docs (checked: {:?}",
                            path, in_tree_path
                        ),
                    })
                }
            } else {
                do_in_tree_check(doc_line, root_path, docs_folder, path, &in_tree_path)
            }
        }
        // Accept external links.
        Ok(None) if is_external_path(path) => None,
        Ok(None) => Some(DocCheckError {
            doc_line: doc_line.clone(),
            message: format!("invalid path {}", path),
        }),
        Err(e) => Some(DocCheckError {
            doc_line: doc_line.clone(),
            message: format!("Error checking path {}: {}", path, e),
        }),
    }
}

fn check_areas(filename: &Path, yaml_value: &Value) -> Option<Vec<DocCheckError>> {
    //TODO(fxbug.dev/113634): Align _areas.yaml on same schema.
    if filename.ends_with("contribute/governance/areas/_areas.yaml") {
        let (_items, errors) = parse_entries::<AreaEntry>(filename, yaml_value);
        //TODO(fxbug.dev/113635): other checks for AreaEntry?
        errors
    } else {
        let (_items, errors) = parse_entries::<String>(filename, yaml_value);
        //TODO(fxbug.dev/113635): other checks for AreaEntry?
        errors
    }
}

fn check_deprecated_docs(filename: &Path, yaml_value: &Value) -> Option<Vec<DocCheckError>> {
    let result = serde_yaml::from_value::<Deprecations>(yaml_value.clone());
    //TODO(fxbug.dev/113636): Add a check that the to: doc exists.
    match result {
        Ok(_) => None,
        Err(e) => Some(vec![DocCheckError {
            doc_line: DocLine { line_num: 1, file_name: filename.to_path_buf() },
            message: format!("invalid structure {}", e),
        }]),
    }
}

fn check_drivers_areas(filename: &Path, yaml_value: &Value) -> Option<Vec<DocCheckError>> {
    let result = serde_yaml::from_value::<Vec<String>>(yaml_value.clone());
    //TODO(fxbug.dev/113634): Align on common _areas.yaml structure
    match result {
        Ok(_redirects) => None,
        Err(e) => Some(vec![DocCheckError {
            doc_line: DocLine { line_num: 1, file_name: filename.to_path_buf() },
            message: format!("invalid structure for _drivers_areas {}. Data: {:?}", e, yaml_value),
        }]),
    }
}

fn check_drivers_epitaphs(filename: &Path, yaml_value: &Value) -> Option<Vec<DocCheckError>> {
    let (_items, errors) = parse_entries::<DriverEpitaph>(filename, yaml_value);
    //TODO(fxbug.dev/113637): other checks for DriverEpitaph?
    errors
}

fn check_eng_council(filename: &Path, yaml_value: &Value) -> Option<Vec<DocCheckError>> {
    let result = serde_yaml::from_value::<EngCouncil>(yaml_value.clone());
    match result {
        Ok(_redirects) => None,
        Err(e) => Some(vec![DocCheckError {
            doc_line: DocLine { line_num: 1, file_name: filename.to_path_buf() },
            message: format!("invalid structure for EngCouncil {}. Found {:?}", e, yaml_value),
        }]),
    }
}

fn check_glossary(filename: &Path, yaml_value: &Value) -> Option<Vec<DocCheckError>> {
    let (_items, errors) = parse_entries::<GlossaryTerm>(filename, yaml_value);
    //TODO(fxbug.dev/113639): other checks for GlossaryTerm?
    errors
}

fn check_metadata(filename: &Path, yaml_value: &Value) -> Option<Vec<DocCheckError>> {
    let result = serde_yaml::from_value::<Metadata>(yaml_value.clone());
    //TODO(fxbug.dev/113640): Add checks for metadata.
    match result {
        Ok(_redirects) => None,
        Err(e) => Some(vec![DocCheckError {
            doc_line: DocLine { line_num: 1, file_name: filename.to_path_buf() },
            message: format!("invalid structure for _metadata {}. Data: {:?}", e, yaml_value),
        }]),
    }
}

fn check_problems(filename: &Path, yaml_value: &Value) -> Option<Vec<DocCheckError>> {
    let (_items, errors) = parse_entries::<ProblemEntry>(filename, yaml_value);
    //TODO(fxbug.dev/113641): other checks for ProblemEntry?
    errors
}

fn check_redirects(filename: &Path, yaml_value: &Value) -> Option<Vec<DocCheckError>> {
    let result = serde_yaml::from_value::<Redirects>(yaml_value.clone());
    //TODO(fxbug.dev/113642): add valication to redirects.
    match result {
        Ok(_) => None,
        Err(e) => Some(vec![DocCheckError {
            doc_line: DocLine { line_num: 1, file_name: filename.to_path_buf() },
            message: format!("invalid structure {}", e),
        }]),
    }
}

fn check_rfcs(filename: &Path, yaml_value: &Value) -> Option<Vec<DocCheckError>> {
    let (_items, errors) = parse_entries::<RfcEntry>(filename, yaml_value);
    //TODO(fxbug.dev/113643): other checks for RfcEntry?
    errors
}

fn check_roadmap(filename: &Path, yaml_value: &Value) -> Option<Vec<DocCheckError>> {
    let (_items, errors) = parse_entries::<RoadmapEntry>(filename, yaml_value);
    //TODO(fxbug.dev/113644): other checks for RoadmapEntry?
    errors
}

fn check_supported_cpu_architecture(
    filename: &Path,
    yaml_value: &Value,
) -> Option<Vec<DocCheckError>> {
    let result = serde_yaml::from_value::<Vec<String>>(yaml_value.clone());
    //TODO(fxbug.dev/113645): Add validation
    match result {
        Ok(_redirects) => None,
        Err(e) => Some(vec![DocCheckError {
            doc_line: DocLine { line_num: 1, file_name: filename.to_path_buf() },
            message: format!(
                "invalid structure for _supported_cpu_architecture {}. Data: {:?}",
                e, yaml_value
            ),
        }]),
    }
}

fn check_supported_sys_config(filename: &Path, yaml_value: &Value) -> Option<Vec<DocCheckError>> {
    let (_items, errors) = parse_entries::<SysConfigEntry>(filename, yaml_value);
    //TODO(fxbug.dev/113646): other checks for SysConfigEntry?
    errors
}

fn check_tools(filename: &Path, yaml_value: &Value) -> Option<Vec<DocCheckError>> {
    let (_items, errors) = parse_entries::<ToolsEntry>(filename, yaml_value);
    //TODO(fxbug.dev/113647): other checks for ToolsEntry?
    errors
}

/// parses the yaml_value into a list of T elements.
/// returns the items successfully parsed, and any errors encountered.
fn parse_entries<T: DeserializeOwned>(
    filename: &Path,
    yaml_value: &Value,
) -> (Option<Vec<T>>, Option<Vec<DocCheckError>>) {
    if let Some(item_list) = yaml_value.as_sequence() {
        if item_list.is_empty() {
            (
                None,
                Some(vec![DocCheckError {
                    doc_line: DocLine { line_num: 1, file_name: filename.to_path_buf() },
                    message: format!(
                        "unexpected empty list for {:?} file, got {:?}",
                        filename, yaml_value
                    ),
                }]),
            )
        } else {
            let mut errors: Vec<DocCheckError> = vec![];
            let mut items: Vec<T> = vec![];

            for item in item_list {
                let result = serde_yaml::from_value::<T>(item.clone());
                match result {
                    Ok(element) => items.push(element),
                    Err(e) => {
                        errors.push(DocCheckError {
                            doc_line: DocLine { line_num: 1, file_name: filename.to_path_buf() },
                            message: format!(
                                "invalid structure for {:?} entry: {}. Data: {:?}",
                                filename, e, item
                            ),
                        });
                    }
                };
            }
            let ret_items = if items.is_empty() { None } else { Some(items) };
            let ret_errors = if errors.is_empty() { None } else { Some(errors) };
            (ret_items, ret_errors)
        }
    } else {
        (
            None,
            Some(vec![DocCheckError {
                doc_line: DocLine { line_num: 1, file_name: filename.to_path_buf() },
                message: format!(
                    "unable to parse sequence for {:?} file, expected Sequence, got {:?}",
                    filename, yaml_value
                ),
            }]),
        )
    }
}

/// Called from main to register all the checks to preform which are implemented in this module.
pub fn register_yaml_checks(opt: &DocCheckerArgs) -> Result<Vec<Box<dyn DocYamlCheck>>> {
    let checker = YamlChecker {
        root_dir: opt.root.clone(),
        docs_folder: opt.docs_folder.clone(),
        project: opt.project.clone(),
        check_external_links: !opt.local_links_only,
    };

    Ok(vec![Box::new(checker)])
}

#[cfg(test)]
mod test {

    use {
        super::*,
        crate::test::{get_lock, MTX},
    };

    #[test]
    fn test_check_path() -> Result<()> {
        let doc_line = &DocLine { line_num: 1, file_name: PathBuf::from("test-check-path") };
        let root_path = PathBuf::from("/some/root");
        let docs_folder = PathBuf::from("docs");
        let project = "fuchsia";

        let test_data: [(&str, Option<DocCheckError>);7] = [
            ("/CONTRIBUTING.md", None),
            ("/CODE_OF_CONDUCT.md", None),
            ("/README.md", Some(DocCheckError { doc_line: DocLine { line_num: 1, file_name: PathBuf::from("test-check-path") }, message: "invalid path /README.md. Path must be in /docs (checked: \"/README.md\"".to_string() })),
            ("https://fuchsia.dev/reference/to/something-else.md", None),
            ("/docs/are-ok.md", None),
            ("https://somewhere.com/is-ok", None),
            ("/src/main.cc", Some(DocCheckError { doc_line: DocLine { line_num: 1, file_name: PathBuf::from("test-check-path") }, message: "invalid path /src/main.cc. Path must be in /docs (checked: \"/src/main.cc\"".to_string() }))
        ];

        // get the lock for the mock, it is released when
        // the test exits.
        let _m = get_lock(&MTX);

        let exists_ctx = path_helper::exists_context();
        let is_dir_ctx = path_helper::is_dir_context();

        // Make directories exist, and any files but README.md exist.
        exists_ctx.expect().returning(|_| true);
        is_dir_ctx.expect().returning(|p| {
            let path_str = p.to_string_lossy();
            !path_str.ends_with(".md")
        });

        for (test_path, expected_result) in test_data {
            let actual_result = check_path(doc_line, &root_path, &docs_folder, project, test_path);
            assert_eq!(actual_result, expected_result);
        }

        Ok(())
    }

    #[test]
    fn test_check_areas() -> Result<()> {
        // Test is more complex because of todo
        //TODO(fxbug.dev/113634): Align _areas.yaml on same schema.
        let filename = "/some/docs/contribute/governance/areas/_areas.yaml";
        let yaml_value: Value = serde_yaml::from_str(
            r#"
- name: 'Area1'
  api_primary: 'someone@google.com'
  api_secondary: 'someonelese@google.com'
  description: |
          <p>
            This is an area.
          </p>
  examples:
    - fidl: 'fuchsia.docs.samples'
          "#,
        )?;

        assert_eq!(check_areas(&PathBuf::from(filename), &yaml_value), None);

        Ok(())
    }
}
