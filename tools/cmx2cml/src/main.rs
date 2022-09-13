// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::{bail, ensure, Context, Error};
use argh::FromArgs;
use serde::Deserialize;
use std::{
    collections::BTreeSet,
    fmt::Write,
    path::{Path, PathBuf},
};

mod facets;
mod features;
mod program;
mod runner;
mod sandbox;
mod warnings;

use facets::CmxFacets;
use program::CmxProgram;
use runner::RunnerSelection;
use sandbox::CmxSandbox;
use warnings::Warning;

/// EXPERIMENTAL convert CMX files to CML, see //tools/cmx2cml/README.md for details
#[derive(FromArgs, Debug)]
pub struct Opt {
    /// file to process
    #[argh(positional)]
    cmx: Option<PathBuf>,

    /// path to a newline-delimited file with multiple CMXes to convert, intended for use by scripts
    #[argh(option, short = 'f')]
    path_to_cmxes: Option<PathBuf>,

    /// runner to use
    #[argh(option, long = "runner")]
    runner: RunnerSelection,

    /// path to which to write generated CML, defaults to `$(basename $CMX).cml`
    #[argh(option, long = "output")]
    output: Option<PathBuf>,
}

#[fuchsia::main]
fn main() -> Result<(), Error> {
    let Opt { cmx, path_to_cmxes, runner, output } = argh::from_env();
    let mut cmxes = vec![];
    if let Some(cmx) = cmx {
        cmxes.push(cmx);
    }
    if let Some(path) = path_to_cmxes {
        let files = std::fs::read_to_string(path).expect("must be able to read cmx input file");
        cmxes.extend(files.lines().map(PathBuf::from));
    }

    let mut errors = vec![];
    for cmx in cmxes {
        let out_path = if let Some(p) = &output {
            p.to_path_buf()
        } else {
            let mut out_path = cmx.to_owned();
            out_path.set_extension("cml");
            out_path
        };

        if let Err(e) = convert_cmx(&cmx, runner, &out_path)
            .with_context(|| format!("converting {}", cmx.display()))
        {
            errors.push(e);
        }
    }

    if errors.is_empty() {
        Ok(())
    } else {
        let mut err_msges = String::new();

        for e in errors {
            err_msges.push_str(&e.to_string());
            let mut source = e.source();
            while let Some(s) = source {
                write!(err_msges, "\n    └── {}", s)?;
                source = s.source();
            }
            err_msges.push_str("\n\n");
        }

        bail!("{}", err_msges)
    }
}

const COPYRIGHT_HEADER: &str = "// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.\n";

fn convert_cmx(cmx: &Path, runner: RunnerSelection, out_path: &Path) -> Result<(), Error> {
    let in_bytes = std::fs::read_to_string(&cmx).context("reading cmx file")?;

    // use JSON5 because some CMX files have JSON5 elements...sigh
    let value: Cmx = serde_json5::from_str(&in_bytes).context("parsing as Cmx")?;

    // convert the cmx to a cml::Document
    let (cml, warnings) = value.convert(runner).context("converting to Cml")?;

    // get the JSON5 representation and format it
    let output = COPYRIGHT_HEADER.to_string()
        + &serde_json5::to_string(&cml).context("serializing CML to JSON5 repr")?;
    let formatted =
        String::from_utf8(cml::format_cml(&output, &out_path).context("formatting generated CML")?)
            .context("creating string from formatted CML bytes")?;

    // split by lines and add comments for any warnings we encountered
    let mut formatted_lines = formatted.lines().map(|s| s.to_string()).collect::<Vec<_>>();
    for warning in warnings {
        warning.apply(&mut formatted_lines);
    }

    // we format again so that indentation of inserted warning comments lines up
    let reformatted = cml::format_cml(&formatted_lines.join("\n"), &out_path)
        .context("formatting generated CML")?;

    std::fs::write(&out_path, &reformatted)
        .with_context(|| format!("writing to {}", out_path.display()))?;

    Ok(())
}

#[derive(Debug, Deserialize)]
#[serde(deny_unknown_fields)]
struct Cmx {
    include: Option<Vec<String>>,
    program: Option<CmxProgram>,
    runner: Option<String>,
    sandbox: Option<CmxSandbox>,
    facets: Option<CmxFacets>,
}

impl Cmx {
    fn convert(self, runner: RunnerSelection) -> Result<(cml::Document, BTreeSet<Warning>), Error> {
        let mut warnings = BTreeSet::new();
        if let Some(w) = runner.warning() {
            warnings.insert(w);
        }

        let mut include: BTreeSet<String> = if let Some(includes) = self.include {
            includes
                .iter()
                .map(|i| {
                    let mut p = PathBuf::from(i);
                    p.set_extension("cml");
                    p.display().to_string()
                })
                .collect()
        } else {
            Default::default()
        };
        // warn about shard renames if we don't recognize any of them
        if include.iter().any(|i| !KNOWN_INCLUDES.contains(&&**i)) {
            warnings.insert(Warning::IncludesRenamed);
        }

        runner.fix_includes(&mut include);
        ensure!(self.runner.is_none(), "manually-defined runners in cmx files are not supported");

        let program = if let Some(program) = self.program {
            Some(program.convert(runner).context("converting program")?)
        } else {
            None
        };

        let mut uses = vec![];
        let mut children = vec![];
        let mut injected_protocols = BTreeSet::new();
        if let Some(facets) = self.facets {
            injected_protocols = facets
                .convert(&mut include, &mut uses, &mut children, &mut warnings)
                .context("converting test facets")?;
        }

        if let Some(sandbox) = self.sandbox {
            uses.extend(sandbox.uses(
                &mut warnings,
                &mut include,
                &injected_protocols,
                runner.is_for_testing(),
            )?);
        };

        Ok((
            cml::Document {
                program,
                include: if include.is_empty() {
                    None
                } else {
                    Some(include.into_iter().collect())
                },
                r#use: if uses.is_empty() { None } else { Some(uses) },
                children: if children.is_empty() { None } else { Some(children) },

                // we could try to wire up injected children's caps but that's probably more effort
                // than it will take to do so manually for all of the cmx files with injection
                offer: None,

                // v1 does not have a disable section
                disable: None,

                // v1 components don't have this info, users must populate it
                capabilities: None,
                expose: None,

                // v2 components can dynamically launch v1 children until those children are migrated
                collections: None,

                // v2 components support facets but all v1 facets are translated into other things in v2
                facets: None,

                // v1 components don't have equivalents for these
                config: None,
                environments: None,
            },
            warnings,
        ))
    }
}

const BUILD_INFO_PROTOCOL: &str = "fuchsia.buildinfo.Provider";
const PROTOCOLS_FOR_HERMETIC_TESTS: &[&str] = &[
    "fuchsia.boot.WriteOnlyLog",
    "fuchsia.diagnostics.ArchiveAccessor",
    "fuchsia.logger.Log",
    "fuchsia.logger.LogSink",
    "fuchsia.process.Launcher",
    "fuchsia.sys.Environment",
    "fuchsia.sys.Launcher",
    "fuchsia.sys.Loader",
    "fuchsia.sys2.EventSource",
    "fuchsia.tracing.provider.Registry",
];
const PROTOCOLS_FOR_SYSTEM_TESTS: &[&str] = &[
    "fuchsia.boot.ReadOnlyLog",
    "fuchsia.boot.RootResource",
    "fuchsia.component.resolution.Resolver",
    "fuchsia.exception.Handler",
    "fuchsia.hardware.pty.Device",
    "fuchsia.kernel.Counter",
    "fuchsia.kernel.CpuResource",
    "fuchsia.kernel.DebugResource",
    "fuchsia.kernel.HypervisorResource",
    "fuchsia.kernel.InfoResource",
    "fuchsia.kernel.IoportResource",
    "fuchsia.kernel.IrqResource",
    "fuchsia.kernel.MmioResource",
    "fuchsia.kernel.PowerResource",
    "fuchsia.kernel.RootJob",
    "fuchsia.kernel.RootJobForInspect",
    "fuchsia.kernel.SmcResource",
    "fuchsia.kernel.Stats",
    "fuchsia.kernel.VmexResource",
    "fuchsia.net.http.Loader",
    "fuchsia.posix.socket.Provider",
    "fuchsia.scheduler.ProfileProvider",
    "fuchsia.sysinfo.SysInfo",
    "fuchsia.sysmem.Allocator",
    "fuchsia.tracing.provider.Registry",
    "fuchsia.vulkan.loader.Loader",
];

const KNOWN_INCLUDES: &[&str] = &[
    SYSLOG_SHARD,
    ELF_TEST_RUNNER_SHARD,
    GTEST_RUNNER_SHARD,
    GUNIT_RUNNER_SHARD,
    RUST_TEST_RUNNER_SHARD,
    SYSTEM_TEST_SHARD,
    VULKAN_TEST_APP_SHARD,
];

const SYSLOG_SHARD: &str = "syslog/client.shard.cml";
const ELF_TEST_RUNNER_SHARD: &str = "//sdk/lib/sys/testing/elf_test_runner.shard.cml";
const GTEST_RUNNER_SHARD: &str = "//src/sys/test_runners/gtest/default.shard.cml";
const GUNIT_RUNNER_SHARD: &str = "//src/sys/test_runners/gunit/default.shard.cml";
const RUST_TEST_RUNNER_SHARD: &str = "//src/sys/test_runners/rust/default.shard.cml";
const SYSTEM_TEST_SHARD: &str = "sys/testing/system-test.shard.cml";
const VULKAN_TEST_APP_SHARD: &str = "//src/lib/vulkan/test-application.shard.cml";
