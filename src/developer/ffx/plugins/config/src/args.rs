// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    argh::FromArgs,
    ffx_config::{api::query::SelectMode, ConfigLevel},
    ffx_core::ffx_command,
};

#[ffx_command]
#[derive(FromArgs, Debug, PartialEq)]
#[argh(
    subcommand,
    name = "config",
    description = "View and switch default and user configurations"
)]
pub struct ConfigCommand {
    #[argh(subcommand)]
    pub sub: SubCommand,
}

#[derive(FromArgs, Debug, PartialEq)]
#[argh(subcommand)]
pub enum SubCommand {
    Env(EnvCommand),
    Get(GetCommand),
    Set(SetCommand),
    Remove(RemoveCommand),
    Add(AddCommand),
}

#[derive(FromArgs, Debug, PartialEq)]
#[argh(subcommand, name = "set", description = "set config settings")]
pub struct SetCommand {
    #[argh(positional)]
    /// name of the property to set
    pub name: String,

    #[argh(positional)]
    /// value to associate with name
    pub value: String,

    #[argh(option, from_str_fn(parse_level), default = "ConfigLevel::User", short = 'l')]
    /// config level. Possible values are "user", "build", "global". Defaults to "user".
    pub level: ConfigLevel,

    // TODO(fxbug.dev/45493): figure out how to work with build directories.  Is it just the directory
    // from which ffx is called? This will probably go away.
    #[argh(option, short = 'b')]
    /// an optional build directory to associate the build config provided - use used for "build"
    /// configs
    pub build_dir: Option<String>,
}

#[derive(Debug, PartialEq, Copy, Clone)]
pub enum MappingMode {
    Raw,
    Substitute,
    SubstituteAndFlatten,
}

#[derive(FromArgs, Debug, PartialEq)]
#[argh(subcommand, name = "get", description = "display config values")]
pub struct GetCommand {
    #[argh(positional)]
    /// name of the config property
    pub name: Option<String>,

    #[argh(option, from_str_fn(parse_mapping_mode), default = "MappingMode::Raw", short = 'p')]
    /// how to process results. Possible values are "raw", "sub", and "sub_flat".  Defaults
    /// to "raw". Currently only supported if a name is given.
    pub process: MappingMode,

    #[argh(option, from_str_fn(parse_mode), default = "SelectMode::First", short = 's')]
    /// how to collect results. Possible values are "first" and "all".  Defaults to
    /// "first".  If the value is "first", the first value found in terms of priority is returned.
    /// If the value is "all", all values across all configuration levels are aggregrated and
    /// returned. Currently only supported if a name is given.
    pub select: SelectMode,

    // TODO(fxbug.dev/45493): figure out how to work with build directories.  Is it just the directory
    // from which ffx is called? This will probably go away.
    #[argh(option, short = 'b')]
    /// an optional build directory to associate the build config provided - use used for "build"
    /// configs
    pub build_dir: Option<String>,
}

#[derive(FromArgs, Debug, PartialEq)]
#[argh(
    subcommand,
    name = "remove",
    description = "remove config for a given level",
    note = "This will remove the entire value for the given name.  If the value is a subtree or \
       array, the entire subtree or array will be removed.  If you want to remove a specific value \
       from an array, consider editing the configuration file directly.  Configuration file \
       locations can be found by running `ffx config env get` command."
)]
pub struct RemoveCommand {
    #[argh(positional)]
    /// name of the config property
    pub name: String,

    #[argh(option, from_str_fn(parse_level), default = "ConfigLevel::User", short = 'l')]
    /// config level. Possible values are "user", "build", "global". Defaults to "user".
    pub level: ConfigLevel,

    // TODO(fxbug.dev/45493): figure out how to work with build directories.  Is it just the directory
    // from which ffx is called? This will probably go away.
    #[argh(option, short = 'b')]
    /// an optional build directory to associate the build config provided - use used for "build"
    /// configs
    pub build_dir: Option<String>,
}

#[derive(FromArgs, Debug, PartialEq)]
#[argh(
    subcommand,
    name = "add",
    description = "add config value the end of an array",
    note = "This will always add to the end of an array.  Adding to a subtree is not supported. \
        If the current value is not an array, it will convert the value to an array.  If you want \
        to insert a value in a different position, consider editing the configuration file \
        directly.  Configuration file locations can be found by running `ffx config env get` \
        command."
)]
pub struct AddCommand {
    #[argh(positional)]
    /// name of the property to set
    pub name: String,

    #[argh(positional)]
    /// value to add to name
    pub value: String,

    #[argh(option, from_str_fn(parse_level), default = "ConfigLevel::User", short = 'l')]
    /// config level. Possible values are "user", "build", "global". Defaults to "user".
    pub level: ConfigLevel,

    // TODO(fxbug.dev/45493): figure out how to work with build directories.  Is it just the directory
    // from which ffx is called? This will probably go away.
    #[argh(option, short = 'b')]
    /// an optional build directory to associate the build config provided - use used for "build"
    /// configs
    pub build_dir: Option<String>,
}

#[derive(FromArgs, Debug, PartialEq)]
#[argh(subcommand, name = "env", description = "list environment settings")]
pub struct EnvCommand {
    #[argh(subcommand)]
    pub access: Option<EnvAccessCommand>,
}

#[derive(FromArgs, Debug, PartialEq)]
#[argh(subcommand)]
pub enum EnvAccessCommand {
    Set(EnvSetCommand),
    Get(EnvGetCommand),
}

#[derive(FromArgs, Debug, PartialEq)]
#[argh(subcommand, name = "set", description = "set environment settings")]
pub struct EnvSetCommand {
    #[argh(positional)]
    /// path to the config file for the configruation level provided
    pub file: String,

    #[argh(option, from_str_fn(parse_level), default = "ConfigLevel::User", short = 'l')]
    /// config level. Possible values are "user", "build", "global". Defaults to "user".
    pub level: ConfigLevel,

    #[argh(option, short = 'b')]
    /// an optional build directory to associate the build config provided - use used for "build"
    /// configs
    pub build_dir: Option<String>,
}

#[derive(FromArgs, Debug, PartialEq)]
#[argh(subcommand, name = "get", description = "list environment for a given level")]
pub struct EnvGetCommand {
    #[argh(positional, from_str_fn(parse_level))]
    /// config level. Possible values are "user", "build", "global".
    pub level: Option<ConfigLevel>,
}

fn parse_level(value: &str) -> Result<ConfigLevel, String> {
    match value {
        "u" | "user" => Ok(ConfigLevel::User),
        "b" | "build" => Ok(ConfigLevel::Build),
        "g" | "global" => Ok(ConfigLevel::Global),
        _ => Err(String::from(
            "Unrecognized value. Possible values are \"user\",\"build\",\"global\".",
        )),
    }
}

fn parse_mapping_mode(value: &str) -> Result<MappingMode, String> {
    match value {
        "r" | "raw" => Ok(MappingMode::Raw),
        "s" | "sub" | "substitute" => Ok(MappingMode::Substitute),
        "sf" | "sub_flat" | "sub_and_flat" | "substitute_and_flatten" => {
            Ok(MappingMode::SubstituteAndFlatten)
        }
        _ => Err(String::from(
            "Unrecognized value. Possible values are \"raw\",\"sub\",\"sub_and_flat\".",
        )),
    }
}

fn parse_mode(value: &str) -> Result<SelectMode, String> {
    match value {
        "f" | "first" | "first_found" => Ok(SelectMode::First),
        "a" | "all" | "add" | "additive" => Ok(SelectMode::All),
        _ => Err(String::from(
            "Unrecognized value. Possible values are \"first_found\" or \"additive\".",
        )),
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    const CMD_NAME: &'static [&'static str] = &["config"];

    #[test]
    fn test_env_get() {
        fn check(args: &[&str], expected_level: Option<ConfigLevel>) {
            assert_eq!(
                ConfigCommand::from_args(CMD_NAME, args),
                Ok(ConfigCommand {
                    sub: SubCommand::Env(EnvCommand {
                        access: Some(EnvAccessCommand::Get(EnvGetCommand {
                            level: expected_level,
                        })),
                    })
                })
            )
        }

        let levels = [
            ("build", Some(ConfigLevel::Build)),
            ("user", Some(ConfigLevel::User)),
            ("global", Some(ConfigLevel::Global)),
        ];

        for level_opt in levels.iter() {
            check(&["env", "get", &level_opt.0], level_opt.1);
        }
    }

    #[test]
    fn test_env_set() {
        fn check(args: &[&str], expected_level: ConfigLevel) {
            assert_eq!(
                ConfigCommand::from_args(CMD_NAME, args),
                Ok(ConfigCommand {
                    sub: SubCommand::Env(EnvCommand {
                        access: Some(EnvAccessCommand::Set(EnvSetCommand {
                            level: expected_level,
                            file: "/test/config.json".to_string(),
                            build_dir: Some("/test/".to_string()),
                        })),
                    })
                })
            )
        }

        let levels = [
            ("build", ConfigLevel::Build),
            ("user", ConfigLevel::User),
            ("global", ConfigLevel::Global),
        ];

        for level_opt in levels.iter() {
            check(
                &[
                    "env",
                    "set",
                    "/test/config.json",
                    "--level",
                    &level_opt.0,
                    "--build-dir",
                    "/test/",
                ],
                level_opt.1,
            );
        }
    }

    #[test]
    fn test_get() {
        fn check(args: &[&str], expected_key: &str, expected_build_dir: Option<String>) {
            assert_eq!(
                ConfigCommand::from_args(CMD_NAME, args),
                Ok(ConfigCommand {
                    sub: SubCommand::Get(GetCommand {
                        process: MappingMode::Raw,
                        select: SelectMode::First,
                        name: Some(expected_key.to_string()),
                        build_dir: expected_build_dir,
                    })
                })
            )
        }

        let key = "test-key";
        let build_dir = "/test/";
        check(&["get", key], key, None);
        check(&["get", key, "--build-dir", build_dir], key, Some(build_dir.to_string()));
    }

    #[test]
    fn test_set() {
        fn check(
            args: &[&str],
            expected_level: ConfigLevel,
            expected_key: &str,
            expected_value: &str,
            expected_build_dir: Option<String>,
        ) {
            assert_eq!(
                ConfigCommand::from_args(CMD_NAME, args),
                Ok(ConfigCommand {
                    sub: SubCommand::Set(SetCommand {
                        level: expected_level,
                        name: expected_key.to_string(),
                        value: expected_value.to_string(),
                        build_dir: expected_build_dir,
                    })
                })
            )
        }

        let key = "test-key";
        let value = "test-value";
        let build_dir = "/test/";
        let levels = [
            ("build", ConfigLevel::Build),
            ("user", ConfigLevel::User),
            ("global", ConfigLevel::Global),
        ];

        for level_opt in levels.iter() {
            check(&["set", key, value, "--level", level_opt.0], level_opt.1, key, value, None);
            check(
                &["set", key, value, "--level", level_opt.0, "--build-dir", build_dir],
                level_opt.1,
                key,
                value,
                Some(build_dir.to_string()),
            );
        }
    }

    #[test]
    fn test_remove() {
        fn check(
            args: &[&str],
            expected_level: ConfigLevel,
            expected_key: &str,
            expected_build_dir: Option<String>,
        ) {
            assert_eq!(
                ConfigCommand::from_args(CMD_NAME, args),
                Ok(ConfigCommand {
                    sub: SubCommand::Remove(RemoveCommand {
                        level: expected_level,
                        name: expected_key.to_string(),
                        build_dir: expected_build_dir,
                    })
                })
            )
        }

        let key = "test-key";
        let build_dir = "/test/";
        let levels = [
            ("build", ConfigLevel::Build),
            ("user", ConfigLevel::User),
            ("global", ConfigLevel::Global),
        ];

        for level_opt in levels.iter() {
            check(&["remove", key, "--level", level_opt.0], level_opt.1, key, None);
            check(
                &["remove", key, "--level", level_opt.0, "--build-dir", build_dir],
                level_opt.1,
                key,
                Some(build_dir.to_string()),
            );
        }
    }
}
