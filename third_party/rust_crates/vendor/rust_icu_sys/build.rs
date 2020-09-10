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

/// This is all a no-op if `use-bindgen` disabled.
#[cfg(feature = "use-bindgen")]
mod inner {
    use {
        anyhow::{Context, Result},
        bindgen,
        lazy_static::lazy_static,
        std::env,
        std::fs::File,
        std::io::Write,
        std::path::Path,
        std::process,
    };

    lazy_static! {
        // The modules for which bindings will be generated.  Add more if you need them.  The list
        // should be topologicaly sorted based on the inclusion relationship between the respective
        // headers.  Any of these will fail if the required binaries are not present in $PATH.
        static ref BINDGEN_SOURCE_MODULES: Vec<&'static str> = vec![
            "ucal",
            "uclean",
            "ucol",
            "udat",
            "udata",
            "uenum",
            "ufieldpositer",
            "uformattable",
            "ulistformatter",
            "umisc",
            "umsg",
            "unum",
            "unumberformatter",
            "upluralrules",
            "uset",
            "ustring",
            "utext",
            "utrans",
        ];

        // C functions that will be made available to rust code.  Add more to this list if you want to
        // bring in more types.
        static ref BINDGEN_ALLOWLIST_FUNCTIONS: Vec<&'static str> = vec![
            "u_.*",
            "ucal_.*",
            "ucol_.*",
            "udat_.*",
            "udata_.*",
            "uenum_.*",
            "ufieldpositer_.*",
            "ufmt_.*",
            "ulistfmt_.*",
            "uloc_.*",
            "umsg_.*",
            "unum_.*",
            "unumf_.*",
            "uplrules_.*",
            "utext_.*",
            "utrans_.*",
        ];

        // C types that will be made available to rust code.  Add more to this list if you want to
        // generate more bindings.
        static ref BINDGEN_ALLOWLIST_TYPES: Vec<&'static str> = vec![
            "UAcceptResult",
            "UBool",
            "UCalendar.*",
            "UChar.*",
            "UCol.*",
            "UCollation.*",
            "UCollator",
            "UData.*",
            "UDate.*",
            "UDateFormat.*",
            "UDisplayContext.*",
            "UEnumeration.*",
            "UErrorCode",
            "UField.*",
            "UFormat.*",
            "UFormattedList.*",
            "UListFormatter.*",
            "ULoc.*",
            "ULOC.*",
            "UMessageFormat",
            "UNUM.*",
            "UNumber.*",
            "UParseError",
            "UPlural.*",
            "USet",
            "UText",
            "UTransDirection",
            "UTransPosition",
            "UTransliterator",
        ];
    }

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

    /// A command representing an auto-configuration detector.  Use `ICUConfig::new()` to create.
    struct ICUConfig {
        rep: Command,
    }

    impl ICUConfig {
        /// Creates a new ICUConfig.
        fn new() -> Self {
            ICUConfig {
                rep: Command::new("pkg-config"),
            }
        }
        /// Obtains the prefix directory, e.g. `$HOME/local`
        fn prefix(&mut self) -> Result<String> {
            self.rep
                .run(&["--variable=prefix", "icu-i18n"])
                .with_context(|| format!("could not get config prefix"))
        }

        /// Obtains the default library path for the libraries.
        fn libdir(&mut self) -> Result<String> {
            self.rep
                .run(&["--variable=libdir", "icu-i18n"])
                .with_context(|| format!("could not get library directory"))
        }

        /// Obtains the needed flags for the linker.
        fn ldflags(&mut self) -> Result<String> {
            // Replacements needed because of https://github.com/rust-lang/cargo/issues/7217
            let result = self
                .rep
                .run(&["--libs", "icu-i18n"])
                .with_context(|| format!("could not get the ld flags"))?;
            Ok(result.replace("-L", "-L ").replace("-l", "-l "))
        }

        /// Obtains the needed flags for the C++ compiler.
        fn cppflags(&mut self) -> Result<String> {
            self.rep
                .run(&["--cflags", "icu-i18n"])
                .with_context(|| format!("while getting the cpp flags"))
        }

        /// Obtains the major-minor version number for the library. Returns a string like `64.2`.
        fn version(&mut self) -> Result<String> {
            self.rep
                .run(&["--modversion", "icu-i18n"])
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
        fn version_major_int() -> Result<i32> {
            let version_str = ICUConfig::version_major()?;
            Ok(version_str.parse().unwrap())
        }
    }

    /// Returns true if the ICU library was compiled with renaming enabled.
    fn has_renaming() -> Result<bool> {
        let cpp_flags = ICUConfig::new().cppflags()?;
        let found = cpp_flags.find("-DU_DISABLE_RENAMING=1");
        Ok(found.is_none())
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

    fn run_bindgen(header_file: &str, out_dir_path: &Path) -> Result<()> {
        let mut builder = bindgen::Builder::default()
            .header(header_file)
            .default_enum_style(bindgen::EnumVariation::Rust {
                non_exhaustive: false,
            })
            // Bindings are pretty much unreadable without rustfmt.
            .rustfmt_bindings(true)
            // Some comments get recognized as rust doctests, which will fail compilation.
            // Turning the comments off will remove that error.  We do get left without
            // documentation, but one should probably use online docs anyways.
            .generate_comments(false)
            // These attributes are useful to have around for generated types.
            .derive_default(true)
            .derive_hash(true)
            .derive_partialord(true)
            .derive_partialeq(true);

        // Add all types that should be exposed to rust code.
        for bindgen_type in BINDGEN_ALLOWLIST_TYPES.iter() {
            builder = builder.whitelist_type(bindgen_type);
        }

        // Add all functions that should be exposed to rust code.
        for bindgen_function in BINDGEN_ALLOWLIST_FUNCTIONS.iter() {
            builder = builder.whitelist_function(bindgen_function);
        }

        // Add the correct clang settings.
        let renaming_arg =
            match has_renaming().with_context(|| "could not prepare bindgen builder")? {
                true => "",
                // When renaming is disabled, the functions will have a suffix that
                // represents the library version in use, for example funct_64 for ICU
                // version 64.
                false => "-DU_DISABLE_RENAMING=1",
            };
        let builder = builder.clang_arg(renaming_arg);
        let ld_flags = ICUConfig::new()
            .ldflags()
            .with_context(|| "could not prepare bindgen builder")?;
        let builder = builder.clang_arg(&ld_flags);
        let cpp_flags = ICUConfig::new()
            .cppflags()
            .with_context(|| "could not prepare bindgen builder")?;
        let builder = builder.clang_arg(cpp_flags);

        let bindings = builder
            .generate()
            .map_err(|_| anyhow::anyhow!("could not generate bindings"))?;

        let output_file_path = out_dir_path.join("lib.rs");
        let output_file = output_file_path.to_str().unwrap();
        bindings
            .write_to_file(output_file)
            .with_context(|| format!("while writing output"))?;
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
    pub fn copy_features() -> Result<()> {
        if let Some(_) = env::var_os("CARGO_FEATURE_RENAMING") {
            println!("cargo:rustc-cfg=feature=\"renaming\"");
        }
        if let Some(_) = env::var_os("CARGO_FEATURE_USE_BINDGEN") {
            println!("cargo:rustc-cfg=feature=\"use-bindgen\"");
        }
        if let Some(_) = env::var_os("CARGO_FEATURE_ICU_CONFIG") {
            println!("cargo:rustc-cfg=feature=\"icu_config\"");
        }
        if let Some(_) = env::var_os("CARGO_FEATURE_ICU_VERSION_IN_ENV") {
            println!("cargo:rustc-cfg=feature=\"icu_version_in_env\"");
        }
        if ICUConfig::version_major_int()? >= 67 {
            println!("cargo:rustc-cfg=feature=\"icu_version_67_plus\"");
        }
        if ICUConfig::version_major_int()? >= 67 {
            println!("cargo:rustc-cfg=feature=\"icu_version_67_plus\"");
        }
        Ok(())
    }

    pub fn icu_config_autodetect() -> Result<()> {
        println!("icu-version: {}", ICUConfig::new().version()?);
        println!("icu-cppflags: {}", ICUConfig::new().cppflags()?);
        println!("icu-has-renaming: {}", has_renaming()?);

        // The path to the directory where cargo will add the output artifacts.
        let out_dir = env::var("OUT_DIR").unwrap();
        let out_dir_path = Path::new(&out_dir);

        // The path where all unicode headers can be found.
        let include_dir_path = Path::new(&ICUConfig::new().prefix()?)
            .join("include")
            .join("unicode");

        let header_file =
            generate_wrapper_header(&out_dir_path, &BINDGEN_SOURCE_MODULES, &include_dir_path);
        run_bindgen(&header_file, out_dir_path)
            .with_context(|| format!("while running bindgen"))?;
        run_renamegen(out_dir_path).with_context(|| format!("while running renamegen"))?;

        println!("cargo:install-dir={}", ICUConfig::new().install_dir()?);

        let lib_dir = ICUConfig::new().libdir()?;
        println!("cargo:rustc-link-search=native={}", lib_dir);
        println!("cargo:rustc-flags={}", ICUConfig::new().ldflags()?);

        Ok(())
    }
}

#[cfg(feature = "use-bindgen")]
fn main() -> Result<(), anyhow::Error> {
    std::env::set_var("RUST_BACKTRACE", "full");
    inner::copy_features()?;
    if let None = std::env::var_os("CARGO_FEATURE_ICU_CONFIG") {
        return Ok(());
    }
    inner::icu_config_autodetect()?;
    println!("done:true");
    Ok(())
}

/// No-op if use-bindgen is disabled.
#[cfg(not(feature = "use-bindgen"))]
fn main() {}
