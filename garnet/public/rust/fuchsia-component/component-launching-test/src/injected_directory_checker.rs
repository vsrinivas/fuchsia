fn main() {
    let contents =
        std::fs::read_to_string("/injected_dir/injected_file").expect("read injected file");
    assert_eq!(contents, "injected file contents");
}
