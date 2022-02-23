// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use argh::FromArgs;
use ffx_core::ffx_command;

#[ffx_command()]
#[derive(FromArgs, Debug, PartialEq)]
#[argh(subcommand, name = "volume_policy")]
/// read or configure volume policy
pub struct VolumePolicy {
    #[argh(subcommand)]
    pub subcommand: SubcommandEnum,
}

#[derive(FromArgs, PartialEq, Debug)]
#[argh(subcommand)]
pub enum SubcommandEnum {
    /// adds a policy transform.
    Add(AddArgs),
    /// gets the current policy.
    Get(GetArgs),
    /// removes a policy transform by its policy ID.
    Remove(RemoveArgs),
}

#[derive(FromArgs, Debug, PartialEq, Clone)]
#[argh(subcommand, name = "add", description = "Adds a new volume policy.")]
/// adds a new volume policy
pub struct AddArgs {
    /// target stream to apply the policy transform to. Valid options are (non-case sensitive):
    /// background, b; media, m; interruption, i; system_agent, s; communication, c.
    #[argh(positional, from_str_fn(str_to_audio_stream))]
    pub target: fidl_fuchsia_media::AudioRenderUsage,

    /// the minimum allowed value for the target
    #[argh(option)]
    pub min: Option<f32>,

    /// the maximum allowed value for the target
    #[argh(option)]
    pub max: Option<f32>,
}

#[derive(FromArgs, PartialEq, Debug)]
#[argh(subcommand, name = "get", description = "Get the current volume policy.")]
pub struct GetArgs {}

#[derive(FromArgs, PartialEq, Debug, Clone)]
#[argh(subcommand, name = "remove", description = "Remove a policy transform by its policy ID.")]
pub struct RemoveArgs {
    /// removes a policy transform by its policy ID.
    #[argh(option, short = 'p')]
    pub policy_id: u32,
}

fn str_to_audio_stream(src: &str) -> Result<fidl_fuchsia_media::AudioRenderUsage, String> {
    match src.to_lowercase().as_str() {
        "background" | "b" => Ok(fidl_fuchsia_media::AudioRenderUsage::Background),
        "media" | "m" => Ok(fidl_fuchsia_media::AudioRenderUsage::Media),
        "interruption" | "i" => Ok(fidl_fuchsia_media::AudioRenderUsage::Interruption),
        "system_agent" | "s" => Ok(fidl_fuchsia_media::AudioRenderUsage::SystemAgent),
        "communication" | "c" => Ok(fidl_fuchsia_media::AudioRenderUsage::Communication),
        _ => Err(String::from("Couldn't parse audio stream type")),
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    const CMD_NAME: &'static [&'static str] = &["volume_policy"];

    #[test]
    fn test_volume_policy_add_cmd() {
        // Test input arguments are generated to according struct.
        let target = "media";
        let min = "0.5";
        let args = &["add", target, "--min", min];
        assert_eq!(
            VolumePolicy::from_args(CMD_NAME, args),
            Ok(VolumePolicy {
                subcommand: SubcommandEnum::Add(AddArgs {
                    target: str_to_audio_stream(target).unwrap(),
                    min: Some(0.5),
                    max: None
                })
            })
        )
    }

    #[test]
    fn test_volume_policy_get_cmd() {
        // Test input arguments are generated to according struct.
        let args = &["get"];
        assert_eq!(
            VolumePolicy::from_args(CMD_NAME, args),
            Ok(VolumePolicy { subcommand: SubcommandEnum::Get(GetArgs {}) })
        )
    }

    #[test]
    fn test_volume_policy_remove_cmd() {
        // Test input arguments are generated to according struct.
        let id = "12";
        let args = &["remove", "-p", id];
        assert_eq!(
            VolumePolicy::from_args(CMD_NAME, args),
            Ok(VolumePolicy { subcommand: SubcommandEnum::Remove(RemoveArgs { policy_id: 12 }) })
        )
    }
}
