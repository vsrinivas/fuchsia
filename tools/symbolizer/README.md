# Symbolizer

This is a C++ implementation of a Fuchsia log symbolizer. A symbolizer takes text logs as input,
containing unsymbolicated stack traces indicated via a special [Symbolizer markup
format](/docs/reference/kernel/symbolizer_markup.md), and symbolizes them provided debug symbols.

## E2E tests

E2E tests are disabled by default because they depend on the presence of
`//prebuilt/test_data/symbolizer/symbols`, which is not downloaded by default. This could be done by
`jiri init -fetch-optional=symbolizer-test-data && jiri fetch-packages`.

Once there are symbols, the E2E tests could be built by adding `//tools/symbolizer:e2e_tests` to
`args.gn` and executed by `fx test symbolizer_e2e_tests`.
