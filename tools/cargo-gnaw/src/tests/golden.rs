use std::path::PathBuf;

#[test]
fn simple_test() {
    let fuchsia_dir =
        std::env::var("FUCHSIA_ROOT").expect("FUCHSIA_ROOT needs to be set for this test");
    let cargo_path: PathBuf =
        [&fuchsia_dir, "tools", "cargo-gnaw", "src", "tests", "simple", "Cargo.toml"]
            .iter()
            .collect();
    let dir_path: PathBuf = [fuchsia_dir].iter().collect();
    let mut output = vec![];
    crate::generate_from_manifest(&mut output, cargo_path, dir_path, None, false).unwrap();
    let output = String::from_utf8(output).unwrap();
    assert_eq!(include_str!("simple/BUILD.gn"), output);
}

#[test]
fn deps_test() {
    let fuchsia_dir =
        std::env::var("FUCHSIA_ROOT").expect("FUCHSIA_ROOT needs to be set for this test");
    let cargo_path: PathBuf =
        [&fuchsia_dir, "tools", "cargo-gnaw", "src", "tests", "simple_deps", "Cargo.toml"]
            .iter()
            .collect();
    let dir_path: PathBuf = [fuchsia_dir].iter().collect();
    let mut output = vec![];
    crate::generate_from_manifest(&mut output, cargo_path, dir_path, None, false).unwrap();
    let output = String::from_utf8(output).unwrap();
    assert_eq!(include_str!("simple_deps/BUILD.gn"), output);
}

#[test]
fn root_skip_test() {
    let fuchsia_dir =
        std::env::var("FUCHSIA_ROOT").expect("FUCHSIA_ROOT needs to be set for this test");
    let cargo_path: PathBuf =
        [&fuchsia_dir, "tools", "cargo-gnaw", "src", "tests", "simple_deps", "Cargo.toml"]
            .iter()
            .collect();
    let dir_path: PathBuf = [fuchsia_dir].iter().collect();
    let mut output = vec![];
    crate::generate_from_manifest(&mut output, cargo_path, dir_path, None, true).unwrap();
    let output = String::from_utf8(output).unwrap();
    assert_eq!(include_str!("simple_deps/BUILD_WITH_NO_ROOT.gn"), output);
}
