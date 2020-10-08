// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {anyhow::Context, std::ffi::OsString};

fn main() -> Result<(), anyhow::Error> {
    let program = find_program(std::env::args_os())
        .context("unable to determine multi-universal-tool binary name")?;

    match program {
        Program::Pkgctl => pkgctl::main(),
        Program::Update => update::main(),
    }
}

fn find_program(mut args: impl Iterator<Item = OsString>) -> Result<Program, FindProgramError> {
    let name = args
        .next()
        .map(std::path::PathBuf::from)
        .ok_or(FindProgramError::NoArgs)?
        .file_name()
        .ok_or(FindProgramError::FirstArgHasNoFilename)?
        .to_string_lossy()
        .into_owned();

    match name.as_str() {
        "pkgctl" => Ok(Program::Pkgctl),
        "update" => Ok(Program::Update),
        _ => Err(FindProgramError::UnknownProgram(name)),
    }
}

#[derive(Debug, PartialEq, Eq)]
enum Program {
    Pkgctl,
    Update,
}

#[derive(Debug, PartialEq, Eq, thiserror::Error)]
enum FindProgramError {
    #[error("no program arguments")]
    NoArgs,

    #[error("first argument has no filename")]
    FirstArgHasNoFilename,

    #[error("unknown inner binary name: {0}")]
    UnknownProgram(String),
}

#[cfg(test)]
mod tests {
    use super::*;

    macro_rules! args {
        ($( $arg:literal )*) => {
            vec![$(
                std::ffi::OsString::from($arg.to_owned()),
            )*].into_iter()
        }
    }

    #[test]
    fn matches_known_programs() {
        assert_eq!(find_program(args!("/pkg/bin/pkgctl")), Ok(Program::Pkgctl));
        assert_eq!(find_program(args!("/pkg/bin/update")), Ok(Program::Update));
    }

    #[test]
    fn rejects_missing_args() {
        assert_eq!(find_program(args!()), Err(FindProgramError::NoArgs));
    }

    #[test]
    fn rejects_invalid_paths() {
        assert_eq!(find_program(args!("/")), Err(FindProgramError::FirstArgHasNoFilename));
    }

    #[test]
    fn rejects_unknown_program() {
        assert_eq!(
            find_program(args!("/pkg/bin/foo")),
            Err(FindProgramError::UnknownProgram("foo".to_string()))
        );
        assert_eq!(
            find_program(args!("/pkg/bin/foo" "--help")),
            Err(FindProgramError::UnknownProgram("foo".to_string()))
        );
    }
}
