# basic integration test for structured configuration

This test ensures we can create multiple config value files and package the
same component multiple times with different values, reusing the same component
manifest to resolve the expected configuration from each value file.

Config values are defined in the `config/` subdirectory and asserted on in
`src/lib.rs`. Currently only the `my_flag` boolean field is varied between
test runs.

A puppet is defined in each language-specific directory and they all share the
same configuration interface. Each "config receiver" component is launched
by the integration test and the config it returns is checked against expected
values.
