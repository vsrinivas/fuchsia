#![feature(try_trait)]
// Copyright 2019 Google LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

// See LICENSE for licensing information.
//
// This build.rs script tries to generate low-level rust bindings for the current ICU library.
// Please refer to README.md for instructions on how to build the library for
// your use.

use {
    anyhow::{Context, Result},
    std::env,
    std::fs::File,
    std::io::Write,
    std::path::Path,
    std::process,
};

/// A `Command` that also knows its name.
struct Command {
    name: String,
    rep: process::Command,
}

impl Command {
    /// Creates a new command to run, with the executable `name`.
    pub fn new(name: &'static str) -> Self {
        let rep = process::Command::new(&name);
        let name = String::from(name);
        Command { name, rep }
    }

    /// Runs this command with `args` as arguments.
    pub fn run(&mut self, args: &[&str]) -> Result<String> {
        self.rep.args(args);
        let stdout = self.stdout()?;
        Ok(String::from(&stdout).trim().to_string())
    }

    // Captures the stdout of the command.
    fn stdout(&mut self) -> Result<String> {
        let output = self
            .rep
            .output()
            .with_context(|| format!("could not execute command: {}", self.name))?;
        let result = String::from_utf8(output.stdout)
            .with_context(|| format!("could not convert output to UTF8"))?;
        Ok(result.trim().to_string())
    }
}

/// A command representing an `icu-config` run.  Use `ICUConfig::new()` to create.
struct ICUConfig {
    rep: Command,
}

impl ICUConfig {
    /// Creates a new ICUConfig.
    fn new() -> Self {
        ICUConfig {
            rep: Command::new("icu-config"),
        }
    }
    /// Runs `icu-config --prefix`.
    fn prefix(&mut self) -> Result<String> {
        self.rep
            .run(&["--prefix"])
            .with_context(|| format!("could not get config prefix"))
    }

    /// Runs `icu-config --libdir`.
    fn libdir(&mut self) -> Result<String> {
        self.rep
            .run(&["--libdir"])
            .with_context(|| format!("could not get library directory"))
    }
    /// Runs `icu-config --ldflags`.
    fn ldflags(&mut self) -> Result<String> {
        // Replacements needed because of https://github.com/rust-lang/cargo/issues/7217
        let result = self
            .rep
            .run(&["--ldflags"])
            .with_context(|| format!("could not get the ld flags"))?;
        Ok(result.replace("-L", "-L ").replace("-l", "-l "))
    }

    /// Runs `icu-config --cppflags`.
    fn cppflags(&mut self) -> Result<String> {
        self.rep
            .run(&["--cppflags"])
            .with_context(|| format!("while getting the cpp flags"))
    }

    /// Runs `icu-config --version`.  Returns a string like `64.2`.
    fn version(&mut self) -> Result<String> {
        self.rep
            .run(&["--version"])
            .with_context(|| format!("while getting ICU version; is icu-config in $PATH?"))
    }

    fn install_dir(&mut self) -> Result<String> {
        self.prefix()
    }

    /// Returns the config major number.  For example, will return "64" for
    /// version "64.2"
    fn version_major() -> Result<String> {
        let version = ICUConfig::new().version()?;
        let components = version.split(".");
        let last = components
            .take(1)
            .last()
            .with_context(|| format!("could not parse version number: {}", version))?;
        Ok(last.to_string())
    }
}

/// Returns true if the ICU library was compiled with renaming enabled.
fn has_renaming() -> Result<bool> {
    let cpp_flags = ICUConfig::new().cppflags()?;
    let found = cpp_flags.find("-DU_DISABLE_RENAMING=1");
    println!("flags: {}", cpp_flags);
    Ok(found.is_none())
}

fn rustfmt_cmd() -> Command {
    Command::new("rustfmt")
}

fn rustfmt_version() -> Result<String> {
    rustfmt_cmd()
        .run(&["--version"])
        .with_context(|| format!("while getting rustfmt version; is rustfmt in $PATH?"))
}

fn bindgen_cmd() -> Command {
    Command::new("bindgen")
}

fn bindgen_version() -> Result<String> {
    bindgen_cmd()
        .run(&["--version"])
        .with_context(|| format!("while getting bindgen version; is bindgen in $PATH?"))
}

/// Generates an additional include file which contains the linker directives.
/// This is done because cargo does not allow the rustc link directives to be
/// anything other than `-L` and `-l`.
fn generate_linker_file(out_dir_path: &Path, lib_dir: &str, lib_names: &Vec<&str>) {
    let file_path = out_dir_path.join("link.rs");
    let mut linker_file = File::create(&file_path).unwrap();
    let mut content: Vec<String> = vec![];
    for lib in lib_names {
        let linkopt: String = format!(r#"#[link_args="-Wl,-rpath={}/lib{}.so"]"#, lib_dir, lib);
        content.push(linkopt);
    }
    content.push(String::from(r#"extern "C" {}"#));
    linker_file
        .write_all(&content.join("\n").into_bytes())
        .expect("successful write into linker file");
}

/// Generates a wrapper header that includes all headers of interest for binding.
///
/// This is the recommended way to bind complex libraries at the moment.  Returns
/// the full path of the generated wrapper header file.
fn generate_wrapper_header(
    out_dir_path: &Path,
    bindgen_source_modules: &Vec<&str>,
    include_path: &Path,
) -> String {
    let wrapper_path = out_dir_path.join("wrapper.h");
    let mut wrapper_file = File::create(&wrapper_path).unwrap();
    wrapper_file
        .write_all(b"/* Generated file, do not edit. */ \n")
        .unwrap();
    let includes = bindgen_source_modules
        .iter()
        .map(|f| {
            let file_path = include_path.join(format!("{}.h", f));
            let file_path_str = format!("#include \"{}\"\n", file_path.to_str().unwrap());
            println!("include-file: '{}'", file_path.to_str().unwrap());
            file_path_str
        })
        .collect::<String>();
    wrapper_file.write_all(&includes.into_bytes()).unwrap();
    String::from(wrapper_path.to_str().unwrap())
}

fn commaify(s: &Vec<&str>) -> String {
    format!("{}", s.join("|"))
}

fn run_bindgen(header_file: &str, out_dir_path: &Path) -> Result<()> {
    let whitelist_types_regexes = commaify(&vec![
        "UAcceptResult",
        "UBool",
        "UCalendar.*",
        "UChar.*",
        "UData.*",
        "UDate",
        "UDateFormat.*",
        "UEnumeration.*",
        "UErrorCode",
        "UText.*",
    ]);

    let whitelist_functions_regexes = commaify(&vec![
        "u_.*", "ucal_.*", "udata_*", "udat_.*", "uenum_.*", "uloc_.*", "utext_.*",
    ]);

    let opaque_types_regexes = commaify(&vec![]);

    // Common arguments for all bindgen invocations.
    let bindgen_generate_args = vec![
        "--default-enum-style=rust",
        "--no-doc-comments",
        "--with-derive-default",
        "--with-derive-hash",
        "--with-derive-partialord",
        "--with-derive-partialeq",
        "--whitelist-type",
        &whitelist_types_regexes,
        "--whitelist-function",
        &whitelist_functions_regexes,
        "--opaque-type",
        &opaque_types_regexes,
    ];

    let output_file_path = out_dir_path.join("lib.rs");
    let output_file = output_file_path.to_str().unwrap();
    let ld_flags = ICUConfig::new().ldflags()?;
    let cpp_flags = ICUConfig::new().cppflags()?;
    let mut file_args = vec![
        "-o",
        &output_file,
        &header_file,
        "--",
        &ld_flags,
        &cpp_flags,
    ];
    if !has_renaming()? {
        file_args.push("-DU_DISABLE_RENAMING=1");
    }
    let all_args = [&bindgen_generate_args[..], &file_args[..]].concat();
    println!("bindgen-cmdline: {:?}", all_args);
    process::Command::new("bindgen")
        .args(&all_args)
        .spawn()
        .with_context(|| format!("while running bindgen"))?;
    Ok(())
}

// Generates the library renaming macro: this allows us to use renamed function
// names in the resulting low-level bindings library.
fn run_renamegen(out_dir_path: &Path) -> Result<()> {
    let output_file_path = out_dir_path.join("macros.rs");
    let mut macro_file = File::create(&output_file_path)
        .with_context(|| format!("while opening {:?}", output_file_path))?;
    if has_renaming()? {
        println!("renaming: true");
        // The library names have been renamed, need to generate a macro that
        // converts the call of `foo()` into `foo_64()`.
        let icu_major_version = ICUConfig::version_major()?;
        let to_write = format!(
            r#"
// Macros for changing function names.
// Automatically generated by build.rs.

extern crate paste;

// This library was build with version renaming, so rewrite every function name
// with its name with version number appended.

// The macro below will rename a symbol `foo::bar` to `foo::bar_{0}` (where "{0}")
// may be some other number depending on the ICU library in use.
#[cfg(feature="renaming")]
#[macro_export]
macro_rules! versioned_function {{
    ($i:ident) => {{
      paste::expr! {{
        [< $i _{0} >]
      }}
    }}
}}
// This allows the user to override the renaming configuration detected from
// icu-config.
#[cfg(not(feature="renaming"))]
#[macro_export]
macro_rules! versioned_function {{
    ($func_name:path) => {{
        $func_name
    }}
}}
"#,
            icu_major_version
        );
        macro_file
            .write_all(&to_write.into_bytes())
            .with_context(|| format!("while writing macros.rs with renaming"))
    } else {
        // The library names have not been renamed, generating an empty macro
        println!("renaming: false");
        macro_file
            .write_all(
                &r#"
// Macros for changing function names.
// Automatically generated by build.rs.

// There was no renaming in this one, so just short-circuit this macro.
#[macro_export]
macro_rules! versioned_function {{
    ($func_name:path) => {{
        $func_name
    }}
}}
"#
                .to_string()
                .into_bytes(),
            )
            .with_context(|| format!("while writing macros.rs without renaming"))
    }
}

/// Copies the featuers set in `Cargo.toml` into the build script.  Not sure
/// why, but the features seem *ignored* when `build.rs` is used.
fn copy_features() -> Result<()> {
    if let Some(_) = env::var_os("CARGO_FEATURE_RENAMING") {
        println!("cargo:rustc-cfg=features=\"renaming\"");
    }
    if let Some(_) = env::var_os("CARGO_FEATURE_BINDGEN") {
        println!("cargo:rustc-cfg=features=\"bindgen\"");
    }
    if let Some(_) = env::var_os("CARGO_FEATURE_ICU_CONFIG") {
        println!("cargo:rustc-cfg=features=\"icu_config\"");
    }
    if let Some(_) = env::var_os("CARGO_FEATURE_ICU_VERSION_IN_ENV") {
        println!("cargo:rustc-cfg=features=\"icu_version_in_env\"");
    }
    Ok(())
}

fn icu_config_autodetect() -> Result<()> {
    println!("rustfmt: {}", rustfmt_version()?);
    println!("icu-config: {}", ICUConfig::new().version()?);
    println!("icu-config-cpp-flags: {}", ICUConfig::new().cppflags()?);
    println!("icu-config-has-renaming: {}", has_renaming()?);
    println!("bindgen: {}", bindgen_version()?);

    // The path to the directory where cargo will add the output artifacts.
    let out_dir = env::var("OUT_DIR").unwrap();
    let out_dir_path = Path::new(&out_dir);

    // The path where all unicode headers can be found.
    let include_dir_path = Path::new(&ICUConfig::new().prefix()?)
        .join("include")
        .join("unicode");

    let lib_dir = ICUConfig::new().libdir()?;

    generate_linker_file(out_dir_path, &lib_dir, &vec!["icui18n", "icuuc", "icudata"]);
    // The modules for which bindings will be generated.  Add more if you need
    // them.  The list should be topologicaly sorted based on the inclusion
    // relationship between the respective headers.
    // Any of these will fail if the required binaries are not present in $PATH.
    let bindgen_source_modules: Vec<&str> = vec![
        "ucal", "udat", "udata", "uenum", "ustring", "utext", "uclean",
    ];
    let header_file =
        generate_wrapper_header(&out_dir_path, &bindgen_source_modules, &include_dir_path);
    run_bindgen(&header_file, out_dir_path).with_context(|| format!("while running bindgen"))?;
    run_renamegen(out_dir_path).with_context(|| format!("while running renamegen"))?;

    println!("cargo:install-dir={}", ICUConfig::new().install_dir()?);
    println!("cargo:rustc-link-search=native={}", lib_dir);
    println!("cargo:rustc-flags={}", ICUConfig::new().ldflags()?);

    Ok(())
}

fn main() -> Result<()> {
    std::env::set_var("RUST_BACKTRACE", "full");
    copy_features()?;
    if let None = env::var_os("CARGO_FEATURE_ICU_CONFIG") {
        return Ok(());
    }
    icu_config_autodetect()?;
    println!("done:true");
    Ok(())
}
