use std::env;
use std::fs;
use std::path::PathBuf;

fn main() {
    let libstdcxx = cfg!(feature = "libstdcxx");
    let libcxx = cfg!(feature = "libcxx");
    let nothing = cfg!(feature = "nothing");

    if nothing {
        return;
    }

    if libstdcxx && libcxx {
        println!(
            "cargo:warning=-lstdc++ and -lc++ are both requested, \
             using the platform's default"
        );
    }

    match (libstdcxx, libcxx) {
        (true, false) => println!("cargo:rustc-link-lib=stdc++"),
        (false, true) => println!("cargo:rustc-link-lib=c++"),
        (false, false) | (true, true) => {
            // The platform's default.
            let out_dir = env::var_os("OUT_DIR").expect("missing OUT_DIR");
            let path = PathBuf::from(out_dir).join("dummy.cc");
            fs::write(&path, "int rust_link_cplusplus;\n").unwrap();
            cc::Build::new().cpp(true).file(&path).compile("link-cplusplus");
        }
    }
}
