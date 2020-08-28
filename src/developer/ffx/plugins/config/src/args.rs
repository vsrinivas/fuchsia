// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {argh::FromArgs, ffx_config::ConfigLevel, ffx_core::ffx_command};

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

    #[argh(option, from_str_fn(parse_level), default = "ConfigLevel::User")]
    /// config level. Possible values are "user", "build", "global". Defaults to "user".
    pub level: ffx_config::ConfigLevel,

    // TODO(fxb/45493): figure out how to work with build directories.  Is it just the directory
    // from which ffx is called? This will probably go away.
    #[argh(option)]
    /// an optional build directory to associate the build config provided - use used for "build"
    /// configs
    pub build_dir: Option<String>,
}

#[derive(FromArgs, Debug, PartialEq)]
#[argh(subcommand, name = "get", description = "list config for a given level")]
pub struct GetCommand {
    #[argh(positional)]
    /// name of the config property
    pub name: Option<String>,

    #[argh(switch)]
    /// subsitute in environment variables from the system
    pub substitute: bool,

    // TODO(fxb/45493): figure out how to work with build directories.  Is it just the directory
    // from which ffx is called? This will probably go away.
    #[argh(option)]
    /// an optional build directory to associate the build config provided - use used for "build"
    /// configs
    pub build_dir: Option<String>,
}

#[derive(FromArgs, Debug, PartialEq)]
#[argh(subcommand, name = "remove", description = "remove config for a given level")]
pub struct RemoveCommand {
    #[argh(positional)]
    /// name of the config property
    pub name: String,

    #[argh(option, from_str_fn(parse_level), default = "ConfigLevel::User")]
    /// config level. Possible values are "user", "build", "global". Defaults to "user".
    pub level: ConfigLevel,

    // TODO(fxb/45493): figure out how to work with build directories.  Is it just the directory
    // from which ffx is called? This will probably go away.
    #[argh(option)]
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
    #[argh(option)]
    /// path to the config file for the configruation level provided
    pub file: String,

    #[argh(option, from_str_fn(parse_level), default = "ConfigLevel::User")]
    /// config level. Possible values are "user", "build", "global". Defaults to "user".
    pub level: ConfigLevel,

    #[argh(option)]
    /// an optional build directory to associate the build config provided - use used for "build"
    /// configs
    pub build_dir: Option<String>,
}

#[derive(FromArgs, Debug, PartialEq)]
#[argh(subcommand, name = "get", description = "list environment for a given level")]
pub struct EnvGetCommand {
    #[argh(option, from_str_fn(parse_level))]
    /// config level. Possible values are "user", "build", "global".
    pub level: Option<ConfigLevel>,
}

fn parse_level(value: &str) -> Result<ConfigLevel, String> {
    match value {
        "user" => Ok(ConfigLevel::User),
        "build" => Ok(ConfigLevel::Build),
        "global" => Ok(ConfigLevel::Global),
        _ => Err(String::from(
            "Unrecognized value. Possible values are \"user\",\"build\",\"global\".",
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
            check(&["env", "get", "--level", &level_opt.0], level_opt.1);
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
                    "--file",
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
                        substitute: false,
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
