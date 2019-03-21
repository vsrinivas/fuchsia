#[cfg(feature = "build-native-harfbuzz")]
extern crate cmake;
#[cfg(feature = "build-native-harfbuzz")]
extern crate pkg_config;

#[cfg(feature = "build-native-harfbuzz")]
fn main() {
    use std::env;
    use std::process::Command;
    use std::path::PathBuf;

    println!("cargo:rerun-if-env-changed=HARFBUZZ_SYS_NO_PKG_CONFIG");
    if env::var_os("HARFBUZZ_SYS_NO_PKG_CONFIG").is_none() {
        if pkg_config::find_library("harfbuzz").is_ok() {
            return;
        }
    }

    // On Windows, HarfBuzz configures atomics directly; otherwise,
    // it needs assistance from configure to do so.  Just use the makefile
    // build for now elsewhere.
    let target = env::var("TARGET").unwrap();
    if target.contains("windows") {
        let dst = cmake::Config::new("harfbuzz").build();
        println!("cargo:rustc-link-search=native={}/lib", dst.display());
        println!("cargo:rustc-link-lib=static=harfbuzz");
        if target.contains("gnu") {
            println!("cargo:rustc-link-lib=stdc++");
        }
    } else {
        assert!(
            Command::new("make")
                .env("MAKEFLAGS", env::var("CARGO_MAKEFLAGS").unwrap_or_default())
                .args(&["-R", "-f", "makefile.cargo"])
                .status()
                .unwrap()
                .success()
        );

        let out_dir = PathBuf::from(env::var_os("OUT_DIR").unwrap());
        println!(
            "cargo:rustc-link-search=native={}",
            out_dir.join("lib").to_str().unwrap()
        );
        println!("cargo:rustc-link-lib=static=harfbuzz");
    }

    // Dependent crates that need to find hb.h can use DEP_HARFBUZZ_INCLUDE from their build.rs.
    println!(
        "cargo:include={}",
        env::current_dir().unwrap().join("harfbuzz/src").display()
    );
}

#[cfg(not(feature = "build-native-harfbuzz"))]
fn main() {}
