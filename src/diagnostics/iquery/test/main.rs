// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![cfg(test)]

use {
    anyhow::{bail, format_err, Error},
    difference::assert_diff,
    fdio::{SpawnAction, SpawnOptions},
    fidl_fuchsia_sys::ComponentControllerEvent,
    fuchsia_async as fasync,
    fuchsia_component::client::launch,
    fuchsia_component::server::ServiceFs,
    fuchsia_zircon::{self as zx, AsHandleRef},
    futures::StreamExt,
    glob::glob,
    regex::Regex,
    std::{
        cmp,
        ffi::{CStr, CString},
        fs,
        io::{BufRead, BufReader},
        os::unix::io::AsRawFd,
    },
    tempfile::NamedTempFile,
};

const GOLDEN_PATH: &str = "/pkg/data/iquery_goldens";
const SERVICE_URL: &str =
    "fuchsia-pkg://fuchsia.com/iquery_golden_tests#meta/iquery_example_component.cmx";

struct Golden {
    command_line: String,
    lines: Vec<String>,
}

impl Golden {
    fn load(filename: &str) -> Result<Self, Error> {
        // Read lines skipping all comments
        let file_content = fs::read_to_string(filename)?;
        let mut lines =
            file_content.lines().skip_while(|line| line.starts_with("#")).map(|s| s.to_string());
        if let Some(command_line) = lines.next() {
            if command_line.starts_with("iquery") {
                return Ok(Self { command_line, lines: lines.collect() });
            }
        }
        return Err(format_err!("Bad golden file {}", filename));
    }
}

struct GoldenTest {
    golden: Golden,
}

impl GoldenTest {
    fn new(golden_name: &str) -> Result<Self, Error> {
        let golden_file = format!("{}/{}.txt", GOLDEN_PATH, golden_name.replace("_", "-"));
        let golden = Golden::load(&golden_file)?;
        Ok(Self { golden })
    }

    async fn execute(self) -> Result<(), Error> {
        let mut service_fs = ServiceFs::new();
        let arguments = vec!["--rows=5".to_string(), "--columns=3".to_string()];
        let nested_environment = service_fs.create_nested_environment("test")?;

        let mut app =
            launch(nested_environment.launcher(), SERVICE_URL.to_string(), Some(arguments))?;

        fasync::spawn(service_fs.collect());

        let mut component_stream = app.controller().take_event_stream();
        match component_stream
            .next()
            .await
            .expect("component event stream ended before termination event")?
        {
            ComponentControllerEvent::OnDirectoryReady {} => {
                let hub_path = get_hub_path()?;
                self.validate(&hub_path)?;
                app.kill().map_err(|e| format_err!("failed to kill component: {}", e))
            }
            ComponentControllerEvent::OnTerminated { return_code, termination_reason } => {
                bail!(
                    "Component terminated unexpectedly. Code: {}. Reason: {:?}",
                    return_code,
                    termination_reason
                );
            }
        }
    }

    fn validate(self, hub_path: &str) -> Result<(), Error> {
        let output_file = NamedTempFile::new().expect("failed to create tempfile");
        self.run_iquery(output_file.as_file(), hub_path)?;
        let reader = BufReader::new(output_file);
        let re = Regex::new("/\\d+/").unwrap();
        let reader_lines = reader.lines().map(|r| r.unwrap()).collect::<Vec<String>>();
        let max = cmp::max(self.golden.lines.len(), reader_lines.len());
        let mut output_iter = reader_lines.into_iter();
        let mut golden_iter = self.golden.lines.into_iter();
        for _ in 0..max {
            let golden_line = golden_iter.next().unwrap_or("".to_string());
            let output_line = output_iter.next().unwrap_or("".to_string());
            // Replace paths containing ids with /*/ to remove process or realm ids.
            let output = re.replace_all(&output_line, "/*/");
            assert_diff!(&golden_line, &output, "", 0);
        }
        Ok(())
    }

    fn run_iquery(&self, output_file: &fs::File, hub_path: &str) -> Result<(), Error> {
        // Set the hub path as the dir in which iquery will be executed.
        let mut args = self.golden.command_line.split(" ").collect::<Vec<&str>>();
        let dir_arg = format!("--dir={}", hub_path);
        args.insert(1, &dir_arg);

        // Ensure we use the iquery bundled in this package, not the global one.
        let command = format!("/pkg/bin/{}", args[0]);

        // Run
        let mut actions = [
            SpawnAction::clone_fd(output_file, std::io::stdout().as_raw_fd()),
            SpawnAction::clone_fd(output_file, std::io::stderr().as_raw_fd()),
        ];
        let argv_cstrings: Vec<CString> =
            args.into_iter().map(|a| CString::new(a).expect("CString: failed")).collect();
        let argv: Vec<&CStr> = argv_cstrings.iter().map(|a| a.as_c_str()).collect();
        let job = zx::Job::from(zx::Handle::invalid());
        let process = fdio::spawn_etc(
            &job,
            SpawnOptions::CLONE_ALL,
            CString::new(command).expect("CString: failed").as_c_str(),
            &argv,
            None,
            &mut actions,
        )
        .map_err(|(status, msg)| {
            format_err!("Failed to launch iquery process. status={}, msg={}", status, msg)
        })?;
        process
            .wait_handle(zx::Signals::PROCESS_TERMINATED, zx::Time::INFINITE)
            .expect("Wait for iquery process termination failed");
        let info = process.info().expect("failed to get iquery process info");
        if !info.exited || info.return_code != 0 {
            return Err(format_err!(
                "iquery process returned non-zero exit code ({})",
                info.return_code
            ));
        }
        Ok(())
    }
}

fn get_hub_path() -> Result<String, Error> {
    let glob_query = "/hub/r/test/*/c/iquery_example_component.cmx/*/out";
    match glob(&glob_query)?.next() {
        Some(found_path_result) => found_path_result
            .map(|p| p.to_string_lossy().to_string())
            .map_err(|e| format_err!("Failed reading out dir: {}", e)),
        None => return Err(format_err!("Out dir not found")),
    }
}

macro_rules! tests {
    ($($test_name:ident,)*) => {
        $(
            #[fasync::run_singlethreaded(test)]
            async fn $test_name() -> Result<(), Error> {
                let test = GoldenTest::new(stringify!($test_name))?;
                test.execute().await
            }
        )*
    }
}

tests![
    cat_recursive_absolute,
    cat_recursive_full,
    cat_recursive_json_absolute,
    cat_recursive_json_full,
    cat_recursive_json,
    cat_recursive,
    cat_single_absolute,
    cat_single_full,
    cat_single,
    explicit_file_full,
    explicit_file,
    find_recursive_json,
    find_recursive,
    find,
    ls_json_absolute,
    ls_json_full,
    ls_json,
    ls,
    report_json,
    report,
    vmo_cat_recursive_absolute,
    vmo_cat_recursive_full,
    vmo_cat_recursive_json_absolute,
    vmo_cat_recursive_json_full,
    vmo_cat_recursive_json,
    vmo_cat_recursive,
    vmo_cat_single_absolute,
    vmo_cat_single_full,
    vmo_cat_single,
    vmo_ls_json_absolute,
    vmo_ls_json_full,
    vmo_ls_json,
    vmo_ls,
    tree_cat_recursive_absolute,
    tree_cat_recursive_full,
    tree_cat_recursive_json_absolute,
    tree_cat_recursive_json_full,
    tree_cat_recursive_json,
    tree_cat_recursive,
    tree_cat_single_absolute,
    tree_cat_single_full,
    tree_cat_single,
    tree_ls_json_absolute,
    tree_ls_json_full,
    tree_ls_json,
    tree_ls,
];
