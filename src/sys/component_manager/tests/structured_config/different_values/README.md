# different values integration test for structured configuration

This test ensures we can create multiple config value files and package the
same component multiple times with different values, reusing the same component
manifest to resolve the expected configuration from each value file.

Config values are defined in the `config/` subdirectory and asserted on in
`src/lib.rs`. Currently only the `my_flag` boolean field is varied between
test runs.
