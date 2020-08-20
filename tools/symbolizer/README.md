# Symbolizer

This is a C++ implementation of a Fuchsia log symbolizer. A symbolizer takes text logs as input,
containing unsymbolicated stack traces indicated via a special [Symbolizer markup
format](/docs/reference/kernel/symbolizer_markup.md), and symbolizes them provided debug symbols.

This is a work in progress. The legacy Go symbolizer at `//tools/debug/symbolize` should still be
used.
