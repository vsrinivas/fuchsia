// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    cm_rust::data::DictionaryExt, fidl_fuchsia_data as fdata, fidl_fuchsia_sys2 as fsys,
    std::path::Path, thiserror::Error,
};

/// An error encountered operating on `ComponentStartInfo`.
#[derive(Debug, PartialEq, Eq, Error)]
pub enum StartInfoError {
    #[error("missing url")]
    MissingUrl,
}

/// An error encountered trying to get entry out of `ComponentStartInfo->program`.
#[derive(Debug, PartialEq, Eq, Error)]
pub enum StartInfoProgramError {
    #[error("\"program.binary\" must be specified")]
    MissingBinary,

    #[error("the value of \"program.binary\" must be a string")]
    InValidBinaryType,

    #[error("the value of \"program.binary\" must be a relative path")]
    BinaryPathNotRelative,

    #[error("invalid type in arguments")]
    InvalidArguments,

    #[error("\"program\" must be specified")]
    NotFound,
}

// Retrieves component URL from start_info or errors out if not found.
pub fn get_resolved_url(start_info: &fsys::ComponentStartInfo) -> Result<String, StartInfoError> {
    match &start_info.resolved_url {
        Some(url) => Ok(url.to_string()),
        _ => Err(StartInfoError::MissingUrl),
    }
}

/// Retrieves program.binary from ComponentStartInfo and makes sure that path is relative.
pub fn get_program_binary(
    start_info: &fsys::ComponentStartInfo,
) -> Result<String, StartInfoProgramError> {
    if let Some(program) = &start_info.program {
        if let Some(val) = program.find("binary") {
            if let fdata::Value::Str(bin) = val {
                if !Path::new(bin).is_absolute() {
                    Ok(bin.to_string())
                } else {
                    Err(StartInfoProgramError::BinaryPathNotRelative)
                }
            } else {
                Err(StartInfoProgramError::InValidBinaryType)
            }
        } else {
            Err(StartInfoProgramError::MissingBinary)
        }
    } else {
        Err(StartInfoProgramError::NotFound)
    }
}

/// Retrieves program.args from ComponentStartInfo and validates them.
pub fn get_program_args(
    start_info: &fsys::ComponentStartInfo,
) -> Result<Vec<String>, StartInfoProgramError> {
    if let Some(program) = &start_info.program {
        if let Some(args) = program.find("args") {
            if let fdata::Value::Vec(vec) = args {
                return vec
                    .values
                    .iter()
                    .map(|v| {
                        if let Some(fdata::Value::Str(a)) = v.as_ref().map(|x| &**x) {
                            Ok(a.clone())
                        } else {
                            Err(StartInfoProgramError::InvalidArguments)
                        }
                    })
                    .collect();
            }
        }
    }
    Ok(vec![])
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn get_resolved_url_test() {
        let new_start_info = |url: Option<String>| fsys::ComponentStartInfo {
            resolved_url: url,
            program: None,
            ns: None,
            outgoing_dir: None,
            runtime_dir: None,
        };
        assert_eq!(
            Ok("some_url".to_string()),
            get_resolved_url(&new_start_info(Some("some_url".to_owned()))),
        );

        assert_eq!(Err(StartInfoError::MissingUrl), get_resolved_url(&new_start_info(None)));
    }

    #[test]
    fn get_program_binary_test() {
        let new_start_info = |binary_name: Option<&str>| fsys::ComponentStartInfo {
            program: Some(fdata::Dictionary {
                entries: vec![fdata::Entry {
                    key: "binary".to_string(),
                    value: binary_name
                        .and_then(|s| Some(Box::new(fdata::Value::Str(s.to_string())))),
                }],
            }),
            ns: None,
            outgoing_dir: None,
            runtime_dir: None,
            resolved_url: None,
        };
        assert_eq!(
            Ok("bin/myexecutable".to_string()),
            get_program_binary(&new_start_info(Some("bin/myexecutable"))),
        );
        assert_eq!(
            Err(StartInfoProgramError::BinaryPathNotRelative),
            get_program_binary(&new_start_info(Some("/bin/myexecutable")))
        );
        assert_eq!(
            Err(StartInfoProgramError::MissingBinary),
            get_program_binary(&new_start_info(None))
        );
    }

    fn new_args_set(args: Vec<Option<Box<fdata::Value>>>) -> fsys::ComponentStartInfo {
        fsys::ComponentStartInfo {
            program: Some(fdata::Dictionary {
                entries: vec![fdata::Entry {
                    key: "args".to_string(),
                    value: Some(Box::new(fdata::Value::Vec(fdata::Vector { values: args }))),
                }],
            }),
            ns: None,
            outgoing_dir: None,
            runtime_dir: None,
            resolved_url: None,
        }
    }

    #[test]
    fn get_program_args_test() {
        let e: Vec<String> = vec![];

        assert_eq!(
            e,
            get_program_args(&fsys::ComponentStartInfo {
                program: Some(fdata::Dictionary { entries: vec![] }),
                ns: None,
                outgoing_dir: None,
                runtime_dir: None,
                resolved_url: None,
            })
            .unwrap()
        );

        assert_eq!(e, get_program_args(&new_args_set(vec![])).unwrap());

        assert_eq!(
            Ok(vec!["a".to_string()]),
            get_program_args(&new_args_set(vec![Some(Box::new(fdata::Value::Str(
                "a".to_string()
            )))]))
        );

        assert_eq!(
            Ok(vec!["a".to_string(), "b".to_string()]),
            get_program_args(&new_args_set(vec![
                Some(Box::new(fdata::Value::Str("a".to_string()))),
                Some(Box::new(fdata::Value::Str("b".to_string()))),
            ]))
        );

        assert_eq!(
            Err(StartInfoProgramError::InvalidArguments),
            get_program_args(&new_args_set(vec![
                Some(Box::new(fdata::Value::Str("a".to_string()))),
                None,
            ]))
        );

        assert_eq!(
            Err(StartInfoProgramError::InvalidArguments),
            get_program_args(&new_args_set(vec![Some(Box::new(fdata::Value::Inum(1))),]))
        );
    }
}
