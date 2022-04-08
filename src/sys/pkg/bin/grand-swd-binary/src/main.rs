// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {anyhow::Context, std::ffi::OsString};

fn main() -> Result<(), anyhow::Error> {
    let program = find_program(std::env::args_os())
        .context("unable to determine grand-swd-binary binary name")?;

    match program {
        Program::OmahaClient => omaha_client_service::main(),
        Program::PkgCache => pkg_cache::main(),
        Program::PkgResolver => pkg_resolver::main(),
        Program::SystemUpdateCommitter => system_update_committer::main(),
        Program::SystemUpdateConfigurator => system_update_configurator::main(),
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
        "omaha_client_service" => Ok(Program::OmahaClient),
        "pkg_cache" => Ok(Program::PkgCache),
        "pkg_resolver" => Ok(Program::PkgResolver),
        "system_update_committer" => Ok(Program::SystemUpdateCommitter),
        "system_update_configurator" => Ok(Program::SystemUpdateConfigurator),
        _ => Err(FindProgramError::UnknownProgram(name)),
    }
}

#[derive(Debug, PartialEq, Eq)]
enum Program {
    OmahaClient,
    PkgCache,
    PkgResolver,
    SystemUpdateCommitter,
    SystemUpdateConfigurator,
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
        assert_eq!(find_program(args!("/pkg/bin/pkg_resolver")), Ok(Program::PkgResolver));
        assert_eq!(
            find_program(args!("/pkg/bin/system_update_committer")),
            Ok(Program::SystemUpdateCommitter)
        );
        assert_eq!(
            find_program(args!("/pkg/bin/system_update_configurator")),
            Ok(Program::SystemUpdateConfigurator)
        );
        assert_eq!(find_program(args!("/pkg/bin/pkg_cache")), Ok(Program::PkgCache));
        assert_eq!(find_program(args!("/pkg/bin/omaha_client_service")), Ok(Program::OmahaClient));
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
