#[cfg(feature = "serde_tests")]
mod serde_tests {
    use std::env;
    use std::path::Path;

    extern crate serde_codegen;

    pub fn build() {
        let out_dir = env::var_os("OUT_DIR").unwrap();

        let src = Path::new("tests/serde_tests.rs.in");
        let dst = Path::new(&out_dir).join("serde_tests.rs");

        serde_codegen::expand(&src, &dst).unwrap();
    }
}

#[cfg(not(feature = "serde_tests"))]
mod serde_tests {
    pub fn build() {}
}

fn main() {
    serde_tests::build()
}
