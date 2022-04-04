# `example_not_configured`

Asserts that product assembly will fail to execute when provided with a
product config that references the assembly example without also specifying
the example-specific config flag. This helps ensure that including the example
code in our production ffx plugin doesn't restrict the namespace of packages
that can be included in a Fuchsia image by downstream product assemblers.
