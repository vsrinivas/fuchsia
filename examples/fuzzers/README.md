# Example Fuzzers

Please note: some of the specific behaviors of these fuzzers are currently
relied upon for manual end-to-end and integration testing, so when making any
changes please ensure that `fx fuzz e2etest` still passes and update it if
necessary. For `cpp:crash_fuzzer` and `cpp:overflow_fuzzer` in particular,
behavioral changes should additionally be validated against the ClusterFuzz
integration tests. See fxbug.dev/61973 for more details.
