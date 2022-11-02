// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! toc_checker checks the _toc.yaml files that part of the //docs publishing process
//! for correctness.

use {
    super::check_path,
    crate::checker::{DocCheckError, DocLine},
    anyhow::Result,
    serde::{Deserialize, Serialize},
    serde_yaml::{Mapping, Value},
    std::{
        fs::File,
        io::BufReader,
        path::{Path, PathBuf},
    },
};

const KNOWN_STATUS: [&str; 7] =
    ["alpha", "beta", "deprecated", "experimental", "external", "limited", "new"];

#[derive(Deserialize, Serialize, PartialEq, Debug)]
struct TocEntry {
    // The fields are valid keys. They are optional,
    // so additional validation should be done to make
    // sure they are consistent.
    alternate_paths: Option<Vec<String>>,
    #[serde(alias = "break")]
    vertical_break: Option<bool>,
    contents: Option<Vec<TocEntry>>,
    heading: Option<String>,
    include: Option<String>,
    name: Option<String>,
    path: Option<String>,
    path_attributes: Option<Vec<Mapping>>,
    section: Option<Vec<TocEntry>>,
    skip_translation: Option<bool>,
    status: Option<String>,
    step_group: Option<String>,
    style: Option<String>,
    title: Option<String>,
}

impl TocEntry {
    pub(crate) fn get_includes(&self) -> Option<Vec<String>> {
        let mut paths: Vec<String> = vec![];
        if let Some(p) = &self.include {
            paths.push(p.to_string());
        }
        if let Some(contents) = &self.contents {
            let path_list: Vec<String> =
                contents.iter().filter_map(|entry| entry.get_includes()).flatten().collect();
            paths.extend(path_list);
        }
        if let Some(section) = &self.section {
            let path_list: Vec<String> =
                section.iter().filter_map(|entry| entry.get_includes()).flatten().collect();
            paths.extend(path_list);
        }

        if paths.is_empty() {
            None
        } else {
            Some(paths)
        }
    }
    pub(crate) fn get_paths(&self) -> Option<Vec<String>> {
        let mut paths: Vec<String> = vec![];
        if let Some(p) = &self.path {
            paths.push(p.to_string())
        }

        if let Some(toc) = &self.contents {
            toc.iter().filter_map(|entry| entry.get_paths()).flatten().for_each(|p| paths.push(p))
        }

        if let Some(toc) = &self.section {
            toc.iter().filter_map(|entry| entry.get_paths()).flatten().for_each(|p| paths.push(p))
        }

        if paths.is_empty() {
            None
        } else {
            Some(paths)
        }
    }
}

#[derive(Serialize, Deserialize, PartialEq, Debug)]
pub(crate) struct Toc {
    toc: Vec<TocEntry>,
}

impl Toc {
    pub(crate) fn from(filepath: &PathBuf) -> Result<Self> {
        let f = File::open(filepath)?;
        let val: Toc = serde_yaml::from_reader(BufReader::new(f))?;
        Ok(val)
    }

    pub(crate) fn get_paths(&self) -> Option<Vec<String>> {
        let paths: Vec<String> =
            self.toc.iter().filter_map(|entry| entry.get_paths()).flatten().collect();
        if paths.is_empty() {
            None
        } else {
            Some(paths)
        }
    }
    pub(crate) fn get_includes(&self) -> Option<Vec<String>> {
        let paths: Vec<String> =
            self.toc.iter().filter_map(|entry| entry.get_includes()).flatten().collect();
        if paths.is_empty() {
            None
        } else {
            Some(paths)
        }
    }
}

pub(crate) fn check_toc(
    root_dir: &Path,
    docs_folder: &Path,
    project: &str,
    filename: &Path,
    yaml_value: &Value,
) -> Option<Vec<DocCheckError>> {
    let doc_line = &DocLine { line_num: 1, file_name: filename.to_path_buf() };
    let result = serde_yaml::from_value::<Toc>(yaml_value.clone());
    let mut errors: Vec<DocCheckError> = vec![];
    match result {
        Ok(toc) => {
            if toc.toc.is_empty() {
                errors.push(DocCheckError {
                    doc_line: DocLine { line_num: 1, file_name: filename.to_path_buf() },
                    message: format!("toc cannot be empty. {:?}.", &yaml_value),
                });
            }
            for toc_entry in toc.toc {
                if let Some(path) = toc_entry.path.as_ref() {
                    // This is a shorthand used in _toc.yaml paths.
                    let path_to_check = if path.starts_with("//") {
                        format!("https:{}", path)
                    } else {
                        path.to_string()
                    };
                    if let Some(e) =
                        check_path(doc_line, root_dir, docs_folder, project, &path_to_check)
                    {
                        errors.push(e);
                    }
                }
                if let Some(status) = toc_entry.status.as_ref() {
                    if !KNOWN_STATUS.contains(&status.as_str()) {
                        errors.push(DocCheckError {
                            doc_line: DocLine { line_num: 1, file_name: filename.to_path_buf() },
                            message: format!(
                                "invalid status {}. Valid statuses are {:?}.",
                                &status, KNOWN_STATUS
                            ),
                        })
                    }
                }
                if toc_entry.step_group.is_some() {
                    // Cannot have Section, and needs Path
                    if toc_entry.section.is_some() {
                        errors.push(DocCheckError {
                            doc_line: DocLine { line_num: 1, file_name: filename.to_path_buf() },
                            message: format!(
                                "invalid toc_entry {:?}. Cannot specify step_group and section.",
                                &toc_entry
                            ),
                        })
                    }
                    if toc_entry.path.is_none() {
                        errors.push(DocCheckError {
                            doc_line: DocLine { line_num: 1, file_name: filename.to_path_buf() },
                            message: format!(
                                "invalid toc_entry {:?}. Cannot specify step_group and not path.",
                                &toc_entry
                            ),
                        })
                    }
                }
                if let Some(style) = toc_entry.style.as_ref() {
                    if !["divider", "accordion"].contains(&style.as_str()) {
                        errors.push(DocCheckError {
                        doc_line: DocLine {
                            line_num: 1,
                            file_name: filename.to_path_buf(),
                        },
                        message: format!("invalid toc_entry {:?}. style must be  one of [\"divider\", \"accordion\"].", &toc_entry),
                    })
                    }
                    if toc_entry.vertical_break.is_some() {
                        errors.push(DocCheckError {
                        doc_line: DocLine {
                            line_num: 1,
                            file_name: filename.to_path_buf(),
                        },
                        message: format!("invalid toc_entry {:?}. Cannot use break, include, style are mutually exclusive", &toc_entry),
                    })
                    }
                    if toc_entry.include.is_some() {
                        errors.push(DocCheckError {
                        doc_line: DocLine {
                            line_num: 1,
                            file_name: filename.to_path_buf(),
                        },
                        message: format!("invalid toc_entry {:?}. Cannot use break, include, style are mutually exclusive", &toc_entry),
                    })
                    }
                    if toc_entry.heading.is_none() && toc_entry.section.is_none() {
                        errors.push(DocCheckError {
                        doc_line: DocLine {
                            line_num: 1,
                            file_name: filename.to_path_buf(),
                        },
                        message: format!("invalid toc_entry {:?}. Use of style requires \"heading\" or \"section\".", &toc_entry),
                    })
                    }
                }
            }
            if !errors.is_empty() {
                Some(errors)
            } else {
                None
            }
        }
        Err(e) => Some(vec![DocCheckError {
            doc_line: DocLine { line_num: 1, file_name: filename.to_path_buf() },
            message: format!("invalid structure {}", e),
        }]),
    }
}

#[cfg(test)]
mod test {
    use crate::mock_path_helper_module as path_helper;
    use {
        super::*,
        crate::test::{get_lock, MTX},
    };

    #[test]
    fn test_empty_toc() -> Result<()> {
        let root_dir = PathBuf::from("/some/root/dir");
        let docs_folder = PathBuf::from("/docs");
        let filename = PathBuf::from("_toc.yaml");
        let project = "some_test_project";
        let toc = Toc { toc: vec![] };

        let yaml_value = serde_yaml::to_value(&toc)?;
        if let Some(result) = check_toc(&root_dir, &docs_folder, project, &filename, &yaml_value) {
            assert_eq!(result.len(), 1);
            if let Some(err) = result.get(0) {
                let expected = DocCheckError {
                    doc_line: DocLine { line_num: 1, file_name: filename.to_path_buf() },
                    message: format!("toc cannot be empty. {:?}.", yaml_value),
                };
                assert_eq!(err, &expected);
            } else {
                panic!("Expected error, but did not get one");
            }
        }
        Ok(())
    }
    #[test]
    fn test_simple_section_toc() -> Result<()> {
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
        //     path_helper.expect_path_exists().returning(|_p| true);
        let root_dir = PathBuf::from("/some/root/dir");
        let filename = PathBuf::from("_toc.yaml");
        let project = "some_test_project";
        let docs_folder = PathBuf::from("/docs");
        let toc = Toc {
            toc: vec![TocEntry {
                alternate_paths: None,
                vertical_break: None,
                contents: None,
                heading: None,
                include: None,
                name: None,
                path: Some(String::from("/docs/title1.md")),
                path_attributes: None,
                section: None,
                skip_translation: None,
                status: None,
                step_group: None,
                style: None,
                title: Some(String::from("Title 1")),
            }],
        };
        let yaml_value = serde_yaml::to_value(&toc)?;
        if let Some(result) = check_toc(&root_dir, &docs_folder, project, &filename, &yaml_value) {
            if let Some(err) = result.get(0) {
                panic!("Unexpected error: {:?}", err)
            }
        }
        Ok(())
    }
    #[test]
    fn test_missing_path() -> Result<()> {
        //  path_helper.expect_path_exists().returning(|_p| false);
        let root_dir = PathBuf::from("/some/root/dir");
        let docs_folder = PathBuf::from("/docs");
        let filename = PathBuf::from("_toc.yaml");
        let project = "some_test_project";
        let toc = Toc {
            toc: vec![TocEntry {
                alternate_paths: None,
                vertical_break: None,
                contents: None,
                heading: None,
                include: None,
                name: None,
                path: Some(String::from("/docs/title1.md")),
                path_attributes: None,
                section: None,
                skip_translation: None,
                status: None,
                step_group: None,
                style: None,
                title: Some(String::from("Title 1")),
            }],
        };

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

        let yaml_value = serde_yaml::to_value(&toc)?;
        if let Some(result) = check_toc(&root_dir, &docs_folder, project, &filename, &yaml_value) {
            assert_eq!(result.len(), 1);
            if let Some(err) = result.get(0) {
                let expected = DocCheckError { doc_line: DocLine { line_num: 1, file_name: PathBuf::from("_toc.yaml") },
                message: "in-tree link to /docs/title1.md could not be found at \"/some/root/dir/docs/title1.md\"".to_string() };
                assert_eq!(err, &expected);
            } else {
                panic!("Expected error, but did not get one");
            }
        }
        Ok(())
    }
    #[test]
    fn test_external_path() -> Result<()> {
        let root_dir = PathBuf::from("/some/root/dir");
        let docs_folder = PathBuf::from("/docs");
        let filename = PathBuf::from("_toc.yaml");
        let project = "some_test_project";
        let toc = Toc {
            toc: vec![
                TocEntry {
                    alternate_paths: None,
                    vertical_break: None,
                    contents: None,
                    heading: None,
                    include: None,
                    name: None,
                    path: Some(String::from("http://some.server/path")),
                    path_attributes: None,
                    section: None,
                    skip_translation: None,
                    status: Some("external".to_string()),
                    step_group: None,
                    style: None,
                    title: Some(String::from("External 1")),
                },
                TocEntry {
                    alternate_paths: None,
                    vertical_break: None,
                    contents: None,
                    heading: None,
                    include: None,
                    name: None,
                    path: Some(String::from(
                        "https://fuchsia.googlesource.com/fuchsia/+/HEAD/src/some_file.cc",
                    )),
                    path_attributes: None,
                    section: None,
                    skip_translation: None,
                    status: Some("external".to_string()),
                    step_group: None,
                    style: None,
                    title: Some(String::from("External 1")),
                },
            ],
        };
        let yaml_value = serde_yaml::to_value(&toc)?;
        if let Some(result) = check_toc(&root_dir, &docs_folder, project, &filename, &yaml_value) {
            panic!("Expected no errors, but got {:?}", result);
        }
        Ok(())
    }
    #[test]
    fn test_path_patterns() -> Result<()> {
        let root_dir = PathBuf::from("/some/root/dir");
        let docs_folder = PathBuf::from("/docs");
        let filename = PathBuf::from("_toc.yaml");
        let project = "some_test_project";

        // get the lock for the mock, it is released when
        // the test exits.
        let _m = get_lock(&MTX);

        let exists_ctx = path_helper::exists_context();
        let is_dir_ctx = path_helper::is_dir_context();

        // Make directories exist, and any files but README.md exist.
        exists_ctx.expect().returning(|p| {
            p == PathBuf::from("/some/root/dir/CONTRIBUTING.md")
                || p == PathBuf::from("/some/root/dir/CODE_OF_CONDUCT.md")
                || (p.starts_with("/some/root/dir/docs") && !p.to_string_lossy().contains("/../"))
        });
        is_dir_ctx.expect().returning(|p| {
            let path_str = p.to_string_lossy();
            !path_str.ends_with(".md")
        });
        /*
        path_helper.expect_path_exists().returning(|p| {
            if p == PathBuf::from("/some/root/dir/CONTRIBUTING.md")
                || p == PathBuf::from("/some/root/dir/CODE_OF_CONDUCT.md")
            {
                true
            } else {
                p.starts_with("/some/root/dir/docs") && !p.to_string_lossy().contains("/../")
            }
        });
        */
        let toc = Toc {
            toc: vec![
                TocEntry {
                    alternate_paths: None,
                    vertical_break: None,
                    contents: None,
                    heading: None,
                    include: None,
                    name: None,
                    path: Some(String::from("http://some.server/path")),
                    path_attributes: None,
                    section: None,
                    skip_translation: None,
                    status: Some("external".to_string()),
                    step_group: None,
                    style: None,
                    title: Some(String::from("External 1")),
                },
                TocEntry {
                    alternate_paths: None,
                    vertical_break: None,
                    contents: None,
                    heading: None,
                    include: None,
                    name: None,
                    path: Some(String::from("/docs/README.md")),
                    path_attributes: None,
                    section: None,
                    skip_translation: None,
                    status: Some("new".to_string()),
                    step_group: None,
                    style: None,
                    title: Some(String::from("In-tree")),
                },
                TocEntry {
                    alternate_paths: None,
                    vertical_break: None,
                    contents: None,
                    heading: None,
                    include: None,
                    name: None,
                    path: Some(String::from("https://google.com")),
                    path_attributes: None,
                    section: None,
                    skip_translation: None,
                    status: None,
                    step_group: None,
                    style: None,
                    title: Some(String::from("https path")),
                },
                TocEntry {
                    alternate_paths: None,
                    vertical_break: None,
                    contents: None,
                    heading: None,
                    include: None,
                    name: None,
                    path: Some(String::from("//google.com")),
                    path_attributes: None,
                    section: None,
                    skip_translation: None,
                    status: None,
                    step_group: None,
                    style: None,
                    title: Some(String::from("Shorthand for https path")),
                },
                TocEntry {
                    alternate_paths: None,
                    vertical_break: None,
                    contents: None,
                    heading: None,
                    include: None,
                    name: None,
                    path: Some(String::from("/reference/generated/content.md")),
                    path_attributes: None,
                    section: None,
                    skip_translation: None,
                    status: None,
                    step_group: None,
                    style: None,
                    title: Some(String::from("Reference docs")),
                },
                TocEntry {
                    alternate_paths: None,
                    vertical_break: None,
                    contents: None,
                    heading: None,
                    include: None,
                    name: None,
                    path: Some(String::from("/CONTRIBUTING.md")),
                    path_attributes: None,
                    section: None,
                    skip_translation: None,
                    status: None,
                    step_group: None,
                    style: None,
                    title: Some(String::from("Contributing")),
                },
                TocEntry {
                    alternate_paths: None,
                    vertical_break: None,
                    contents: None,
                    heading: None,
                    include: None,
                    name: None,
                    path: Some(String::from("/CODE_OF_CONDUCT.md")),
                    path_attributes: None,
                    section: None,
                    skip_translation: None,
                    status: None,
                    step_group: None,
                    style: None,
                    title: Some(String::from("Code of Conduct")),
                },
                TocEntry {
                    alternate_paths: None,
                    vertical_break: None,
                    contents: None,
                    heading: None,
                    include: None,
                    name: None,
                    path: Some(String::from("/src/main.cc")),
                    path_attributes: None,
                    section: None,
                    skip_translation: None,
                    status: None,
                    step_group: None,
                    style: None,
                    title: Some(String::from("Source")),
                },
                TocEntry {
                    alternate_paths: None,
                    vertical_break: None,
                    contents: None,
                    heading: None,
                    include: None,
                    name: None,
                    path: Some(String::from("/docs/../../invalid_path.md")),
                    path_attributes: None,
                    section: None,
                    skip_translation: None,
                    status: None,
                    step_group: None,
                    style: None,
                    title: Some(String::from("Invalid path")),
                },
                TocEntry {
                    alternate_paths: None,
                    vertical_break: None,
                    contents: None,
                    heading: None,
                    include: None,
                    name: None,
                    path: Some(String::from("http://{}.com/markdown")),
                    path_attributes: None,
                    section: None,
                    skip_translation: None,
                    status: None,
                    step_group: None,
                    style: None,
                    title: Some(String::from("Invalid path")),
                },
            ],
        };
        let yaml_value = serde_yaml::to_value(&toc)?;
        if let Some(result) = check_toc(&root_dir, &docs_folder, project, &filename, &yaml_value) {
            let expected_result =[ DocCheckError { doc_line: DocLine { line_num: 1, file_name: PathBuf::from("_toc.yaml") },
            message: "invalid path /src/main.cc. Path must be in /docs (checked: \"/src/main.cc\"".to_string() },
            DocCheckError { doc_line: DocLine { line_num: 1, file_name: PathBuf::from("_toc.yaml") },
             message: "Error checking path /docs/../../invalid_path.md: Cannot normalize /docs/../../invalid_path.md, references parent beyond root.".to_string()},
             DocCheckError { doc_line: DocLine { line_num: 1, file_name: PathBuf::from("_toc.yaml") }, message: "Invalid link http://{}.com/markdown : invalid uri character".to_string() }
            ];

            let mut expected_iter = expected_result.iter();
            for actual in &result {
                if let Some(expected) = expected_iter.next() {
                    assert_eq!(actual, expected);
                } else {
                    panic!(
                        "Too many actual errors, only expected {:?}\n but got {:?}",
                        expected_result, result
                    )
                }
            }
            let unexpected: Vec<&DocCheckError> = expected_iter.collect();
            if !unexpected.is_empty() {
                assert_eq!(result.len(), 1, "Expected  errors, but missing: {:?}", unexpected);
            }
        } else {
            panic!("Expected  errors, but got None");
        }
        Ok(())
    }
}
