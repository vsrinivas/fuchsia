// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::GlobalTargetCfgs,
    crate::{cfg::cfg_to_gn_conditional, target::GnTarget, types::*, CombinedTargetCfg},
    anyhow::{Context, Error},
    cargo_metadata::Package,
    std::borrow::Cow,
    std::collections::BTreeMap,
    std::fmt::Display,
    std::io,
    std::path::Path,
    std::string::ToString,
};

/// Utility to add a version suffix to a GN target name.
pub fn add_version_suffix(prefix: &str, version: &impl ToString) -> String {
    let mut accum = String::new();
    accum.push_str(&prefix);
    accum.push_str("-v");
    accum.push_str(version.to_string().replace(".", "_").as_str());
    accum
}

/// Write a header for the output GN file
pub fn write_header<W: io::Write>(output: &mut W, _cargo_file: &Path) -> Result<(), Error> {
    writeln!(
        output,
        include_str!("../templates/gn_header.template"),
        // TODO set this, but in a way that tests don't fail on Jan 1st
        year = "2020",
    )
    .map_err(Into::into)
}

/// Writes rules at the top of the GN file that don't have the version appended
pub fn write_top_level_rule<'a, W: io::Write>(
    output: &mut W,
    platform: Option<String>,
    pkg: &Package,
) -> Result<(), Error> {
    let target_name = if pkg.is_proc_macro() {
        format!("{}($host_toolchain)", pkg.gn_name())
    } else {
        pkg.gn_name()
    };
    if let Some(ref platform) = platform {
        writeln!(
            output,
            "if ({conditional}) {{\n",
            conditional = cfg_to_gn_conditional(&platform)?
        )?;
    }
    writeln!(
        output,
        include_str!("../templates/entry_gn_rules.template"),
        group_name = pkg.name,
        dep_name = target_name,
    )?;
    if platform.is_some() {
        writeln!(output, "}}\n")?;
    }
    Ok(())
}

/// Writes rules at the top of the GN file that don't have the version appended
pub fn write_binary_top_level_rule<'a, W: io::Write>(
    output: &mut W,
    platform: Option<String>,
    rule_name: &str,
    target: &GnTarget<'a>,
) -> Result<(), Error> {
    if let Some(ref platform) = platform {
        writeln!(
            output,
            "if ({conditional}) {{\n",
            conditional = cfg_to_gn_conditional(&platform)?
        )?;
    }
    writeln!(
        output,
        include_str!("../templates/entry_gn_rules.template"),
        group_name = rule_name,
        dep_name = target.gn_target_name(),
    )?;
    if platform.is_some() {
        writeln!(output, "}}\n")?;
    }
    Ok(())
}

struct GnField {
    ty: String,
    exists: bool,
    // Use BTreeMap so that iteration over platforms is stable.
    add_fields: BTreeMap<Option<Platform>, Vec<String>>,
    remove_fields: BTreeMap<Option<Platform>, Vec<String>>,
}
impl GnField {
    /// If defining a new field in the template
    pub fn new(ty: &str) -> GnField {
        GnField {
            ty: ty.to_string(),
            exists: false,
            add_fields: BTreeMap::new(),
            remove_fields: BTreeMap::new(),
        }
    }

    /// If the field already exists in the template
    pub fn exists(ty: &str) -> GnField {
        GnField { exists: true, ..Self::new(ty) }
    }

    pub fn add_platform_cfg<T: AsRef<str> + Display>(&mut self, platform: Option<String>, cfg: T) {
        let field = self.add_fields.entry(platform).or_insert(vec![]);
        field.push(format!("\"{}\"", cfg));
    }

    pub fn remove_platform_cfg<T: AsRef<str> + Display>(
        &mut self,
        platform: Option<String>,
        cfg: T,
    ) {
        let field = self.remove_fields.entry(platform).or_insert(vec![]);
        field.push(format!("\"{}\"", cfg));
    }

    pub fn add_cfg<T: AsRef<str> + Display>(&mut self, cfg: T) {
        self.add_platform_cfg(None, cfg)
    }

    pub fn remove_cfg<T: AsRef<str> + Display>(&mut self, cfg: T) {
        self.remove_platform_cfg(None, cfg)
    }

    pub fn render_gn(&self) -> String {
        let mut output = if self.exists {
            // We don't create an empty [] if the field already exists
            match self.add_fields.get(&None) {
                Some(add_fields) => format!("{} += [{}]\n", self.ty, add_fields.join(",")),
                None => "".to_string(),
            }
        } else {
            format!("{} = [{}]\n", self.ty, self.add_fields.get(&None).unwrap_or(&vec![]).join(","))
        };

        // remove platfrom independent configs
        if let Some(rm_fields) = self.remove_fields.get(&None) {
            output.push_str(format!("{} -= [{}]\n", self.ty, rm_fields.join(",")).as_str());
        }

        // Add logic for specific platforms
        for platform in self.add_fields.keys().filter(|k| k.is_some()) {
            output.push_str(
                format!(
                    "if ({}) {{\n",
                    cfg_to_gn_conditional(&platform.as_ref().unwrap()).expect("valid cfg")
                )
                .as_str(),
            );
            output.push_str(
                format!(
                    "{} += [{}]",
                    self.ty,
                    self.add_fields.get(platform).unwrap_or(&vec![]).join(",")
                )
                .as_str(),
            );
            output.push_str("}\n");
        }

        // Remove logic for specific platforms
        for platform in self.remove_fields.keys().filter(|k| k.is_some()) {
            output.push_str(
                format!(
                    "if ({}) {{\n",
                    cfg_to_gn_conditional(&platform.as_ref().unwrap()).expect("valid cfg")
                )
                .as_str(),
            );
            output.push_str(
                format!(
                    "{} -= [{}]",
                    self.ty,
                    self.remove_fields.get(platform).unwrap_or(&vec![]).join(",")
                )
                .as_str(),
            );
            output.push_str("}\n");
        }
        output
    }
}

/// Write a Target to the GN file. Includes information from the build script.
pub fn write_rule<W: io::Write>(
    output: &mut W,
    target: &GnTarget<'_>,
    project_root: &Path,
    global_target_cfgs: Option<&GlobalTargetCfgs>,
    custom_build: Option<&CombinedTargetCfg<'_>>,
    output_name: Option<&str>,
) -> Result<(), Error> {
    // Generate a section for dependencies that is paramaterized on toolchain
    let mut dependencies = String::from("deps = []\n");
    let mut aliased_deps = vec![];

    // Stable output of platforms
    let mut platform_deps: Vec<(
        &Option<String>,
        &Vec<(&cargo_metadata::Package, std::string::String)>,
    )> = target.dependencies.iter().collect();
    platform_deps.sort_by(|p, p2| p.0.cmp(p2.0));

    for (platform, deps) in platform_deps {
        // sort for stable output
        let mut deps = deps.clone();
        deps.sort_by(|a, b| (a.0).id.cmp(&(b.0).id));

        // TODO(bwb) feed GN toolchain mapping in as a configuration to make more generic
        match platform.as_ref().map(String::as_str) {
            None => {
                for pkg in deps {
                    dependencies.push_str("  deps += [");
                    if pkg.0.is_proc_macro() {
                        dependencies.push_str(
                            format!("\":{}($host_toolchain)\"", pkg.0.gn_name()).as_str(),
                        );
                    } else {
                        dependencies.push_str(format!("\":{}\"", pkg.0.gn_name()).as_str());
                    }
                    dependencies.push_str("]\n");
                    if pkg.0.name.replace("-", "_") != pkg.1 {
                        aliased_deps.push(format!("{} = \":{}\" ", pkg.1, pkg.0.gn_name()));
                    }
                }
            }
            Some(platform) => {
                dependencies
                    .push_str(format!("if ({}) {{\n", cfg_to_gn_conditional(platform)?).as_str());
                for pkg in deps {
                    dependencies.push_str("  deps += [");
                    if pkg.0.is_proc_macro() {
                        dependencies.push_str(
                            format!("\":{}($host_toolchain)\"", pkg.0.gn_name()).as_str(),
                        );
                    } else {
                        dependencies.push_str(format!("\":{}\"", pkg.0.gn_name()).as_str());
                    }
                    dependencies.push_str("]\n");

                    if pkg.0.name.replace("-", "_") != pkg.1 {
                        aliased_deps.push(format!("{} = \":{}\" ", pkg.1, pkg.0.gn_name()));
                    }
                }
                dependencies.push_str("}\n");
            }
        }
    }

    // write the features into the configs
    let mut rustflags = GnField::new("rustflags");
    let mut rustenv = GnField::new("rustenv");
    let mut configs = GnField::exists("configs");

    if let Some(global_cfg) = global_target_cfgs {
        for cfg in &global_cfg.remove_cfgs {
            configs.remove_cfg(cfg);
        }
        for cfg in &global_cfg.add_cfgs {
            configs.add_cfg(cfg);
        }
    }

    // Associate unique metadata with this crate
    rustflags.add_cfg("--cap-lints=allow");
    rustflags.add_cfg(format!("--edition={}", target.edition));
    rustflags.add_cfg(format!("-Cmetadata={}", target.metadata_hash()));
    rustflags.add_cfg(format!("-Cextra-filename=-{}", target.metadata_hash()));

    // Aggregate feature flags
    for feature in target.features {
        rustflags.add_cfg(format!("--cfg=feature=\\\"{}\\\"", feature));
    }

    // From the gn custom configs, add flags and env vars
    if let Some(custom_build) = custom_build {
        for (platform, cfg) in custom_build {
            if let Some(ref deps) = cfg.deps {
                for dep in deps {
                    // TODO: Respect dep.platform here.
                    dependencies.push_str(format!("  deps += [\"{}\"]", dep).as_str());
                }
            }
            if let Some(ref flags) = cfg.rustflags {
                for flag in flags {
                    rustflags.add_platform_cfg(platform.cloned(), flag.to_string());
                }
            }
            if let Some(ref env_vars) = cfg.env_vars {
                for flag in env_vars {
                    rustenv.add_platform_cfg(platform.cloned(), flag.to_string());
                }
            }
            if let Some(ref crate_configs) = cfg.configs {
                for config in crate_configs {
                    configs.add_platform_cfg(platform.cloned(), config);
                }
            }
        }
    }

    // making the templates more readable.
    let aliased_deps_str = if aliased_deps.len() == 0 {
        String::from("")
    } else {
        format!("aliased_deps = {{{}}}", aliased_deps.join("\n"))
    };

    // GN root relative path
    let root_relative_path = format!(
        "//{}",
        target
            .crate_root
            .strip_prefix(project_root)
            .with_context(|| format!(
                "{} is located outside of the project. Check your vendoring setup",
                target.name()
            ))?
            .to_string_lossy()
    );
    let output_name = output_name.map_or_else(
        || Cow::Owned(format!("{}-{}", target.name().replace("-", "_"), target.metadata_hash())),
        |n| Cow::Borrowed(n),
    );
    writeln!(
        output,
        include_str!("../templates/gn_rule.template"),
        gn_rule = target.gn_target_type(),
        target_name = target.gn_target_name(),
        crate_name = target.name().replace("-", "_"),
        output_name = output_name,
        root_path = root_relative_path,
        aliased_deps = aliased_deps_str,
        dependencies = dependencies,
        cfgs = configs.render_gn(),
        rustenv = rustenv.render_gn(),
        rustflags = rustflags.render_gn(),
    )
    .map_err(Into::into)
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::collections::HashMap;

    #[test]
    fn simple_target() {
        let pkg_id = cargo_metadata::PackageId { repr: String::from("42") };
        let version = semver::Version::new(0, 1, 0);
        let target = GnTarget::new(
            &pkg_id,
            "test_target",
            "test_package",
            "2018",
            Path::new("somewhere/over/the/rainbow.rs"),
            &version,
            GnRustType::Library,
            &[],
            None,
            HashMap::new(),
        );

        let mut output = vec![];
        write_rule(&mut output, &target, Path::new("somewhere/over"), None, None, None).unwrap();
        let output = String::from_utf8(output).unwrap();
        assert_eq!(
            output,
            r#"rust_library("test_package-v0_1_0") {
  crate_name = "test_target"
  crate_root = "//the/rainbow.rs"
  output_name = "test_target-c5bf97c44457465a"
  
  deps = []

  rustenv = []

  rustflags = ["--cap-lints=allow","--edition=2018","-Cmetadata=c5bf97c44457465a","-Cextra-filename=-c5bf97c44457465a"]

  
}

"#
        );
    }
    #[test]
    fn binary_target() {
        let pkg_id = cargo_metadata::PackageId { repr: String::from("42") };
        let version = semver::Version::new(0, 1, 0);
        let target = GnTarget::new(
            &pkg_id,
            "test_target",
            "test_package",
            "2018",
            Path::new("somewhere/over/the/rainbow.rs"),
            &version,
            GnRustType::Binary,
            &[],
            None,
            HashMap::new(),
        );

        let outname = Some("rainbow_binary");
        let mut output = vec![];
        write_rule(&mut output, &target, Path::new("somewhere/over"), None, None, outname).unwrap();
        let output = String::from_utf8(output).unwrap();
        assert_eq!(
            output,
            r#"executable("test_package-test_target-v0_1_0") {
  crate_name = "test_target"
  crate_root = "//the/rainbow.rs"
  output_name = "rainbow_binary"
  
  deps = []

  rustenv = []

  rustflags = ["--cap-lints=allow","--edition=2018","-Cmetadata=bf8f4a806276c599","-Cextra-filename=-bf8f4a806276c599"]

  
}

"#
        );
    }
}
