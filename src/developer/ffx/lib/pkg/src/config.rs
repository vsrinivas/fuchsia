// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{Context, Result},
    ffx_config::{self, ConfigLevel},
    fidl_fuchsia_developer_bridge_ext::{RepositorySpec, RepositoryTarget},
    serde_json::Value,
    std::collections::HashMap,
};

const CONFIG_KEY_REPOSITORIES: &str = "repository.repositories";
const CONFIG_KEY_REGISTRATIONS: &str = "repository.registrations";
const CONFIG_KEY_DEFAULT_REPOSITORY: &str = "repository.default";

fn repository_query(repo_name: &str) -> String {
    format!("{}.{}", CONFIG_KEY_REPOSITORIES, repo_name)
}

fn registration_query(repo_name: &str, target_identifier: &str) -> String {
    format!("{}.{}.{}", CONFIG_KEY_REGISTRATIONS, repo_name, target_identifier)
}

fn repository_registrations_query(repo_name: &str) -> String {
    format!("{}.{}", CONFIG_KEY_REGISTRATIONS, repo_name)
}

/// Return the default repository from the configuration if set.
pub async fn get_default_repository() -> Result<Option<String>> {
    Ok(ffx_config::get(CONFIG_KEY_DEFAULT_REPOSITORY).await?)
}

/// Sets the default repository from the config.
pub async fn set_default_repository(repo_name: &str) -> Result<()> {
    ffx_config::set((CONFIG_KEY_DEFAULT_REPOSITORY, ConfigLevel::User), repo_name.into()).await
}

/// Unsets the default repository from the config.
pub async fn unset_default_repository() -> Result<()> {
    ffx_config::remove((CONFIG_KEY_DEFAULT_REPOSITORY, ConfigLevel::User)).await
}

/// Get repository spec from config.
pub async fn get_repository(repo_name: &str) -> Result<Option<RepositorySpec>> {
    let value = ffx_config::get(&repository_query(repo_name)).await?;
    let repo_spec = serde_json::from_value(value)?;
    Ok(repo_spec)
}

/// Read all the repositories from the config. This will log, but otherwise ignore parse errors.
pub async fn get_repositories() -> HashMap<String, RepositorySpec> {
    let value = match ffx_config::get::<Value, _>(CONFIG_KEY_REPOSITORIES).await {
        Ok(value) => value,
        Err(err) => {
            log::warn!("failed to load repositories: {:#?}", err);
            return HashMap::new();
        }
    };

    let entries = match value {
        Value::Object(entries) => entries,
        _ => {
            log::warn!("expected {} to be a map, not {}", CONFIG_KEY_REPOSITORIES, value);
            return HashMap::new();
        }
    };

    entries
        .into_iter()
        .filter_map(|(name, entry)| {
            // Parse the repository spec.
            match serde_json::from_value(entry) {
                Ok(repo_spec) => Some((name, repo_spec)),
                Err(err) => {
                    log::warn!("failed to parse repository {:?}: {:?}", name, err);
                    None
                }
            }
        })
        .collect()
}

pub async fn set_repository(repo_name: &str, repo_spec: &RepositorySpec) -> Result<()> {
    let repo_spec = serde_json::to_value(repo_spec.clone())?;

    ffx_config::set((&repository_query(repo_name), ConfigLevel::User), repo_spec).await
}

pub async fn remove_repository(repo_name: &str) -> Result<()> {
    ffx_config::remove((&repository_query(repo_name), ConfigLevel::User)).await
}

/// Get the target registration from the config if exists.
pub async fn get_registration(
    repo_name: &str,
    target_identifier: &str,
) -> Result<Option<RepositoryTarget>> {
    let value = ffx_config::get(&registration_query(repo_name, target_identifier)).await?;
    let repo_target = serde_json::from_value(value)?;
    Ok(repo_target)
}

pub async fn get_registrations() -> HashMap<String, HashMap<String, RepositoryTarget>> {
    let value = match ffx_config::get::<Value, _>(CONFIG_KEY_REGISTRATIONS).await {
        Ok(value) => value,
        Err(err) => {
            log::warn!("failed to load registrations: {:#?}", err);
            return HashMap::new();
        }
    };

    let entries = match value {
        Value::Object(entries) => entries,
        _ => {
            log::warn!("expected {} to be a map, not {}", CONFIG_KEY_REGISTRATIONS, value);
            return HashMap::new();
        }
    };

    entries
        .into_iter()
        .map(|(repo_name, targets)| {
            let targets = parse_target_registrations(&repo_name, targets);
            (repo_name, targets)
        })
        .collect()
}

pub async fn get_repository_registrations(repo_name: &str) -> HashMap<String, RepositoryTarget> {
    let targets = match ffx_config::get(&repository_registrations_query(repo_name)).await {
        Ok(targets) => targets,
        Err(err) => {
            log::warn!("failed to load repository registrations: {:?} {:#?}", repo_name, err);
            serde_json::Map::new().into()
        }
    };

    parse_target_registrations(repo_name, targets)
}

fn parse_target_registrations(
    repo_name: &str,
    targets: serde_json::Value,
) -> HashMap<String, RepositoryTarget> {
    let targets = match targets {
        Value::Object(targets) => targets,
        _ => {
            log::warn!("repository {:?} targets should be a map, not {:?}", repo_name, targets);
            return HashMap::new();
        }
    };

    targets
        .into_iter()
        .filter_map(|(target_nodename, target_info)| match serde_json::from_value(target_info) {
            Ok(target_info) => Some((target_nodename, target_info)),
            Err(err) => {
                log::warn!("failed to parse registration {:?}: {:?}", target_nodename, err);
                None
            }
        })
        .collect()
}

pub async fn set_registration(target_nodename: &str, target_info: &RepositoryTarget) -> Result<()> {
    let json_target_info =
        serde_json::to_value(&target_info).context("serializing RepositorySpec")?;

    ffx_config::set(
        (&registration_query(&target_info.repo_name, &target_nodename), ConfigLevel::User),
        json_target_info,
    )
    .await
}

pub async fn remove_registration(repo_name: &str, target_identifier: &str) -> Result<()> {
    ffx_config::remove((&registration_query(repo_name, target_identifier), ConfigLevel::User)).await
}
