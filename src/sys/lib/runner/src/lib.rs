// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

pub mod component;
pub mod log;

use {
    fidl_fuchsia_component_runner as fcrunner, fidl_fuchsia_data as fdata, std::path::Path,
    thiserror::Error,
};

const FORWARD_STDOUT_PROGRAM_KEY: &str = "forward_stdout_to";
const FORWARD_STDERR_PROGRAM_KEY: &str = "forward_stderr_to";

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

    #[error("the value of \"program.forward_stdout_to\" must be 'none' or 'log'")]
    InvalidStdoutSink,

    #[error("the value of \"program.forward_stderr_to\" must be 'none' or 'log'")]
    InvalidStderrSink,
}

/// Target sink for stdout and stderr output streams.
#[derive(Debug, PartialEq, Eq)]
pub enum StreamSink {
    Log,
    None,
}

// Retrieves component URL from start_info or errors out if not found.
pub fn get_resolved_url(
    start_info: &fcrunner::ComponentStartInfo,
) -> Result<String, StartInfoError> {
    match &start_info.resolved_url {
        Some(url) => Ok(url.to_string()),
        _ => Err(StartInfoError::MissingUrl),
    }
}

fn find<'a>(dict: &'a fdata::Dictionary, key: &str) -> Option<&'a fdata::DictionaryValue> {
    match &dict.entries {
        Some(entries) => {
            for entry in entries {
                if entry.key == key {
                    return entry.value.as_ref().map(|val| &**val);
                }
            }
            None
        }
        _ => None,
    }
}

/// Retrieves program.binary from ComponentStartInfo and makes sure that path is relative.
pub fn get_program_binary(
    start_info: &fcrunner::ComponentStartInfo,
) -> Result<String, StartInfoProgramError> {
    if let Some(program) = &start_info.program {
        if let Some(val) = find(program, "binary") {
            if let fdata::DictionaryValue::Str(bin) = val {
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
    start_info: &fcrunner::ComponentStartInfo,
) -> Result<Vec<String>, StartInfoProgramError> {
    if let Some(program) = &start_info.program {
        if let Some(args) = find(program, "args") {
            if let fdata::DictionaryValue::StrVec(vec) = args {
                return vec.iter().map(|v| Ok(v.clone())).collect();
            }
        }
    }
    Ok(vec![])
}

/// Retrieves `program.forward_stdout_to` from ComponentStartInfo and validates.
/// Valid values for this field are: "log", "none".
pub fn get_program_stdout_sink(
    start_info: &fcrunner::ComponentStartInfo,
) -> Result<StreamSink, StartInfoProgramError> {
    get_program_stream_sink_by_key(&start_info, FORWARD_STDOUT_PROGRAM_KEY)
        .ok_or(StartInfoProgramError::InvalidStdoutSink)
}

/// Retrieves `program.forward_stderr_to` from ComponentStartInfo and validates.
/// Valid values for this field are: "log", "none".
pub fn get_program_stderr_sink(
    start_info: &fcrunner::ComponentStartInfo,
) -> Result<StreamSink, StartInfoProgramError> {
    get_program_stream_sink_by_key(&start_info, FORWARD_STDERR_PROGRAM_KEY)
        .ok_or(StartInfoProgramError::InvalidStderrSink)
}

fn get_program_stream_sink_by_key(
    start_info: &fcrunner::ComponentStartInfo,
    key: &str,
) -> Option<StreamSink> {
    let val = if let Some(val) = start_info.program.as_ref().and_then(|program| find(program, key))
    {
        val
    } else {
        return Some(StreamSink::None); // Default to None.
    };

    match val {
        fdata::DictionaryValue::Str(sink) => match sink.as_str() {
            "log" => Some(StreamSink::Log),
            "none" => Some(StreamSink::None),
            _ => None,
        },
        _ => None,
    }
}

#[cfg(test)]
mod tests {
    use {super::*, test_case::test_case};

    #[test_case(Some("some_url"), Ok("some_url".to_owned()) ; "when url is valid")]
    #[test_case(None, Err(StartInfoError::MissingUrl) ; "when url is missing")]
    fn get_resolved_url_test(maybe_url: Option<&str>, expected: Result<String, StartInfoError>) {
        let start_info = fcrunner::ComponentStartInfo {
            resolved_url: maybe_url.map(str::to_owned),
            program: None,
            ns: None,
            outgoing_dir: None,
            runtime_dir: None,
            ..fcrunner::ComponentStartInfo::EMPTY
        };
        assert_eq!(get_resolved_url(&start_info), expected,);
    }

    #[test_case(Some("bin/myexecutable"), Ok("bin/myexecutable".to_owned()) ; "when binary value is valid")]
    #[test_case(Some("/bin/myexecutable"), Err(StartInfoProgramError::BinaryPathNotRelative) ; "when binary path is not relative")]
    #[test_case(None, Err(StartInfoProgramError::NotFound) ; "when program stanza is not set")]
    fn get_program_binary_test(
        maybe_value: Option<&str>,
        expected: Result<String, StartInfoProgramError>,
    ) {
        let start_info = match maybe_value {
            Some(value) => new_start_info(Some(new_program_stanza("binary", value))),
            None => new_start_info(None),
        };
        assert_eq!(get_program_binary(&start_info), expected);
    }

    #[test]
    fn get_program_binary_test_when_binary_key_is_missing() {
        let start_info = new_start_info(Some(new_program_stanza("some_other_key", "bin/foo")));
        assert_eq!(get_program_binary(&start_info), Err(StartInfoProgramError::MissingBinary));
    }

    #[test_case(&[], Ok(vec![]) ; "when args is empty")]
    #[test_case(&["a".to_owned()], Ok(vec!["a".to_owned()]) ; "when args is a")]
    #[test_case(&["a".to_owned(), "b".to_owned()], Ok(vec!["a".to_owned(), "b".to_owned()]) ; "when args a and b")]
    fn get_program_args_test(
        args: &[String],
        expected: Result<Vec<String>, StartInfoProgramError>,
    ) {
        let start_info = new_start_info(Some(new_program_stanza_with_vec("args", Vec::from(args))));
        assert_eq!(get_program_args(&start_info), expected);
    }

    #[test_case(Some("log"), Ok(StreamSink::Log) ; "when value is 'log'")]
    #[test_case(Some("none"), Ok(StreamSink::None) ; "when value is 'none'")]
    #[test_case(None, Ok(StreamSink::None) ; "when value is not set")]
    #[test_case(Some("unknown_value"), Err(StartInfoProgramError::InvalidStdoutSink) ; "when value is invalid")]
    fn get_program_stdout_sink_test(
        maybe_value: Option<&str>,
        expected: Result<StreamSink, StartInfoProgramError>,
    ) {
        let start_info = match maybe_value {
            Some(value) => new_start_info(Some(new_program_stanza("forward_stdout_to", value))),
            None => new_start_info(None),
        };
        assert_eq!(expected, get_program_stdout_sink(&start_info));
    }

    #[test_case(Some("log"), Ok(StreamSink::Log) ; "when value is 'log'")]
    #[test_case(Some("none"), Ok(StreamSink::None) ; "when value is 'none'")]
    #[test_case(None, Ok(StreamSink::None) ; "when value is not set")]
    #[test_case(Some("unknown_value"), Err(StartInfoProgramError::InvalidStderrSink) ; "when value is invalid")]
    fn get_program_stderr_sink_test(
        maybe_value: Option<&str>,
        expected: Result<StreamSink, StartInfoProgramError>,
    ) {
        let start_info = match maybe_value {
            Some(value) => new_start_info(Some(new_program_stanza("forward_stderr_to", value))),
            None => new_start_info(None),
        };
        assert_eq!(expected, get_program_stderr_sink(&start_info));
    }

    #[test_case(
        "forward_stdout_to",
        get_program_stdout_sink,
        Err(StartInfoProgramError::InvalidStdoutSink)
    )]
    #[test_case(
        "forward_stderr_to",
        get_program_stderr_sink,
        Err(StartInfoProgramError::InvalidStderrSink)
    )]
    fn get_program_str_fails_if_value_is_vec(
        key: &str,
        get_fn: fn(&fcrunner::ComponentStartInfo) -> Result<StreamSink, StartInfoProgramError>,
        expected: Result<StreamSink, StartInfoProgramError>,
    ) {
        let values = vec!["a".to_owned()];
        let start_info = new_start_info(Some(new_program_stanza_with_vec(key, values)));
        assert_eq!(expected, get_fn(&start_info));
    }

    fn new_start_info(program: Option<fdata::Dictionary>) -> fcrunner::ComponentStartInfo {
        fcrunner::ComponentStartInfo {
            program: program,
            ns: None,
            outgoing_dir: None,
            runtime_dir: None,
            resolved_url: None,
            ..fcrunner::ComponentStartInfo::EMPTY
        }
    }

    fn new_program_stanza(key: &str, value: &str) -> fdata::Dictionary {
        fdata::Dictionary {
            entries: Some(vec![fdata::DictionaryEntry {
                key: key.to_owned(),
                value: Some(Box::new(fdata::DictionaryValue::Str(value.to_owned()))),
            }]),
            ..fdata::Dictionary::EMPTY
        }
    }

    fn new_program_stanza_with_vec(key: &str, values: Vec<String>) -> fdata::Dictionary {
        fdata::Dictionary {
            entries: Some(vec![fdata::DictionaryEntry {
                key: key.to_owned(),
                value: Some(Box::new(fdata::DictionaryValue::StrVec(values))),
            }]),
            ..fdata::Dictionary::EMPTY
        }
    }
}
