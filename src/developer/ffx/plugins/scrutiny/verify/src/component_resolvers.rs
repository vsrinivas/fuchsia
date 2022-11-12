// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{anyhow, Context, Result},
    ffx_scrutiny_verify_args::component_resolvers::Command,
    scrutiny_config::Config,
    scrutiny_frontend::{command_builder::CommandBuilder, launcher},
    scrutiny_utils::path::join_and_canonicalize,
    serde::{Deserialize, Serialize},
    std::{collections::HashSet, fs, path::PathBuf},
};

type NodePath = String;

#[derive(Debug, Serialize, Deserialize, PartialEq, Eq, PartialOrd, Ord, Clone)]
struct ComponentResolversRequest {
    scheme: String,
    moniker: NodePath,
    protocol: String,
}

#[derive(Debug, Serialize, Deserialize, PartialEq, Eq, Clone)]
struct ComponentResolversResponse {
    deps: HashSet<PathBuf>,
    monikers: Vec<NodePath>,
}

#[derive(Debug, Serialize, Deserialize, PartialEq, Eq, PartialOrd, Ord)]
struct AllowListEntry {
    #[serde(flatten)]
    query: ComponentResolversRequest,
    components: Vec<NodePath>,
}

#[derive(Debug, Serialize, Deserialize, PartialEq, Eq, PartialOrd, Ord)]
struct AllowList(Vec<AllowListEntry>);

impl AllowList {
    pub fn iter(&self) -> impl Iterator<Item = (ComponentResolversRequest, &[NodePath])> {
        self.0.iter().map(|entry| (entry.query.clone(), entry.components.as_slice()))
    }
}

/// A trait to query scrutiny's verify/component_resolvers API.
trait QueryComponentResolvers {
    /// Walk the v2 component tree, finding all components with a component resolver for `scheme`
    /// in its environment that has the given `moniker` and has access to `protocol`.
    fn query(
        &self,
        scheme: String,
        moniker: NodePath,
        protocol: String,
    ) -> Result<ComponentResolversResponse>;
}

/// An impl of [`QueryComponentResolvers`] that launches and queries scrutiny relative to the
/// current working directory.
#[derive(Debug)]
struct ScrutinyQueryComponentResolvers {
    build_path: PathBuf,
    update_package_path: PathBuf,
    blobfs_paths: Vec<PathBuf>,
    tmp_dir_path: Option<PathBuf>,
}

impl QueryComponentResolvers for ScrutinyQueryComponentResolvers {
    fn query(
        &self,
        scheme: String,
        moniker: NodePath,
        protocol: String,
    ) -> Result<ComponentResolversResponse> {
        let request = ComponentResolversRequest { scheme, moniker, protocol };

        let mut config = Config::run_command_with_plugins(
            CommandBuilder::new("verify.component_resolvers")
                .param("scheme", &request.scheme)
                .param("moniker", request.moniker.to_string())
                .param("protocol", &request.protocol)
                .build(),
            vec!["DevmgrConfigPlugin", "StaticPkgsPlugin", "CorePlugin", "VerifyPlugin"],
        );
        config.runtime.model.build_path = self.build_path.clone();
        config.runtime.model.update_package_path = self.update_package_path.clone();
        config.runtime.model.blobfs_paths = self.blobfs_paths.clone();
        config.runtime.model.tmp_dir_path = self.tmp_dir_path.clone();
        config.runtime.logging.silent_mode = true;

        let results = launcher::launch_from_config(config).context("Failed to launch scrutiny")?;
        if results.starts_with("Error: ") {
            return Err(anyhow!(results))
                .with_context(|| format!("Failed to query scrutiny with {:?}", request));
        }
        Ok(serde_json5::from_str(&results).context(format!(
            "Failed to deserialize verify component resolvers results: {:?}",
            results
        ))?)
    }
}

/// For each section of the provided `allowlist`, queries scrutiny for all components configured
/// with a component resolver for `scheme` with the given `moniker` that itself has access
/// to `protocol`.  If any components match but are not in the allowlist, returns an allowlist that
/// would allow all found violations. On success, returns the set of files accessed to run the
/// analysis, for depfile generation.
fn verify_component_resolvers(
    scrutiny: impl QueryComponentResolvers,
    allowlist: AllowList,
) -> Result<Result<HashSet<PathBuf>, AllowList>> {
    let mut violations = vec![];
    let mut deps = HashSet::new();

    for (query, allowed_monikers) in allowlist.iter() {
        let allowed_monikers: HashSet<&NodePath> = allowed_monikers.into_iter().collect();

        let response = scrutiny
            .query(query.scheme.clone(), query.moniker.clone(), query.protocol.clone())
            .with_context(|| {
                format!("Failed to query verify.capability_component_resolvers with {:?}", query)
            })?;
        deps.extend(response.deps);

        let mut unexpected = vec![];

        for moniker in response.monikers {
            if !allowed_monikers.contains(&moniker) {
                unexpected.push(moniker);
            }
        }

        if !unexpected.is_empty() {
            violations.push(AllowListEntry { query, components: unexpected });
        }
    }

    if violations.is_empty() {
        Ok(Ok(deps))
    } else {
        Ok(Err(AllowList(violations)))
    }
}

pub async fn verify(cmd: &Command, tmp_dir: Option<&PathBuf>) -> Result<HashSet<PathBuf>> {
    let allowlist_path = join_and_canonicalize(&cmd.build_path, &cmd.allowlist);
    let update_package_path = join_and_canonicalize(&cmd.build_path, &cmd.update);
    let blobfs_paths =
        cmd.blobfs.iter().map(|blobfs| join_and_canonicalize(&cmd.build_path, blobfs)).collect();
    let scrutiny = ScrutinyQueryComponentResolvers {
        build_path: cmd.build_path.clone(),
        update_package_path,
        blobfs_paths,
        tmp_dir_path: tmp_dir.map(PathBuf::clone),
    };

    let allowlist: AllowList = serde_json5::from_str(
        &fs::read_to_string(&allowlist_path).context("Failed to read allowlist")?,
    )
    .context("Failed to deserialize allowlist")?;

    verify_component_resolvers(scrutiny, allowlist)?.map_err(|violations| {
        anyhow!(
            "
Static Component Resolver Capability Analysis Error:
The component resolver verifier found some components configured to be resolved using
a privileged component resolver.

If it is intended for these components to be resolved using the given resolver, add an entry
to the allowlist located at: {:?}

Verification Errors:
{}",
            allowlist_path,
            serde_json::to_string_pretty(&violations).unwrap()
        )
    })
}

#[cfg(test)]
mod tests {
    use {super::*, assert_matches::assert_matches, std::collections::HashMap};

    #[derive(Debug)]
    struct MockQueryComponentResolvers {
        responses: HashMap<(String, NodePath, String), String>,
    }

    impl MockQueryComponentResolvers {
        fn new() -> Self {
            Self { responses: HashMap::new() }
        }

        fn with_response(
            self,
            query: (String, NodePath, String),
            response: Vec<NodePath>,
            response_deps: Vec<String>,
        ) -> Self {
            let raw_response = serde_json::to_string(&ComponentResolversResponse {
                monikers: response,
                deps: response_deps.into_iter().map(PathBuf::from).collect(),
            })
            .unwrap();
            self.with_raw_response(query, raw_response)
        }

        fn with_raw_response(
            mut self,
            query: (String, NodePath, String),
            response: String,
        ) -> Self {
            self.responses.insert(query, response);
            self
        }
    }

    impl QueryComponentResolvers for MockQueryComponentResolvers {
        fn query(
            &self,
            scheme: String,
            moniker: NodePath,
            protocol: String,
        ) -> Result<ComponentResolversResponse> {
            let key = (scheme, moniker, protocol);

            let response = self
                .responses
                .get(&key)
                .expect(&format!("mock to be configured for key {:?}", key));

            Ok(serde_json5::from_str(&response).context(format!(
                "Failed to deserialize verify component resolvers results: {:?}",
                response
            ))?)
        }
    }

    fn parse_allowlist(raw: &str) -> AllowList {
        let mut allowlist: AllowList = serde_json5::from_str(raw).unwrap();

        for entry in allowlist.0.iter_mut() {
            entry.components.sort_unstable();
        }
        allowlist.0.sort_unstable();

        allowlist
    }

    #[test]
    fn fails_on_invalid_response() {
        let allowlist = parse_allowlist(
            r#"[
            {
                scheme: "fuchsia-pkg",
                moniker: "/core/full-resolver",
                protocol: "fuchsia.pkg.PackageResolver",
                components: [
                ],
            },
        ]"#,
        );

        let scrutiny = MockQueryComponentResolvers::new().with_raw_response(
            (
                "fuchsia-pkg".to_owned(),
                "/core/full-resolver".to_owned(),
                "fuchsia.pkg.PackageResolver".to_owned(),
            ),
            "invalid".to_owned(),
        );

        assert_matches!(verify_component_resolvers(scrutiny, allowlist), Err(_));
    }

    #[test]
    fn reports_unexpected_entry() {
        let allowlist = parse_allowlist(
            r#"[
            {
                scheme: "fuchsia-pkg",
                moniker: "/core/full-resolver",
                protocol: "fuchsia.pkg.PackageResolver",
                components: [
                    "/core/allowed",
                ],
            },
        ]"#,
        );

        let violations = parse_allowlist(
            r#"[
            {
                scheme: "fuchsia-pkg",
                moniker: "/core/full-resolver",
                protocol: "fuchsia.pkg.PackageResolver",
                components: [
                    "/core/stopme",
                ],
            },
        ]"#,
        );

        let scrutiny = MockQueryComponentResolvers::new().with_response(
            (
                "fuchsia-pkg".to_owned(),
                "/core/full-resolver".to_owned(),
                "fuchsia.pkg.PackageResolver".to_owned(),
            ),
            vec!["/core/allowed".to_owned(), "/core/stopme".to_owned()],
            vec!["path/to/dep.zbi".to_owned()],
        );

        assert_eq!(verify_component_resolvers(scrutiny, allowlist).unwrap(), Err(violations));
    }

    #[test]
    fn ignores_unused_allow() {
        let allowlist = parse_allowlist(
            r#"[
            {
                scheme: "fuchsia-pkg",
                moniker: "/core/full-resolver",
                protocol: "fuchsia.pkg.PackageResolver",
                components: [
                    "/core/allowed",
                    "/core/also-allowed",
                ],
            },
        ]"#,
        );

        let scrutiny = MockQueryComponentResolvers::new().with_response(
            (
                "fuchsia-pkg".to_owned(),
                "/core/full-resolver".to_owned(),
                "fuchsia.pkg.PackageResolver".to_owned(),
            ),
            vec!["/core/allowed".to_owned(), "/core/also-allowed".to_owned()],
            vec!["path/to/dep.zbi".to_owned()],
        );

        let expected_deps = vec!["path/to/dep.zbi".to_string().into()].into_iter().collect();
        assert_eq!(verify_component_resolvers(scrutiny, allowlist).unwrap(), Ok(expected_deps));
    }

    #[test]
    fn checks_all_entries() {
        let allowlist = parse_allowlist(
            r#"[
            {
                scheme: "a",
                moniker: "/core/resolver-a",
                protocol: "fuchsia.proto.a",
                components: [
                    "/core/allowed-a",
                ],
            },
            {
                scheme: "b",
                moniker: "/core/resolver-b",
                protocol: "fuchsia.proto.b",
                components: [
                    "/core/allowed-b",
                ],
            },
            {
                scheme: "c",
                moniker: "/core/resolver-c",
                protocol: "fuchsia.proto.c",
                components: [
                    "/core/allowed-c",
                ],
            },
        ]"#,
        );

        let violations = parse_allowlist(
            r#"[
            {
                scheme: "a",
                moniker: "/core/resolver-a",
                protocol: "fuchsia.proto.a",
                components: [
                    "/core/violation-a",
                ],
            },
            {
                scheme: "c",
                moniker: "/core/resolver-c",
                protocol: "fuchsia.proto.c",
                components: [
                    "/core/violation-c",
                ],
            },
        ]"#,
        );

        let scrutiny = MockQueryComponentResolvers::new()
            .with_response(
                ("a".to_owned(), "/core/resolver-a".to_owned(), "fuchsia.proto.a".to_owned()),
                vec!["/core/allowed-a".to_owned(), "/core/violation-a".to_owned()],
                vec!["dep1".to_owned()],
            )
            .with_response(
                ("b".to_owned(), "/core/resolver-b".to_owned(), "fuchsia.proto.b".to_owned()),
                vec!["/core/allowed-b".to_owned()],
                vec!["dep2".to_owned()],
            )
            .with_response(
                ("c".to_owned(), "/core/resolver-c".to_owned(), "fuchsia.proto.c".to_owned()),
                vec!["/core/allowed-c".to_owned(), "/core/violation-c".to_owned()],
                vec!["dep3".to_owned()],
            );

        assert_eq!(verify_component_resolvers(scrutiny, allowlist).unwrap(), Err(violations));
    }
}
