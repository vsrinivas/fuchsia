# ffx scrutiny verify

The `ffx scrutiny verify` command has a number of subcommands that perform
different verification procedures against build artifacts. The command performs
some common logic before and after delegating execution to subcommands. This
pattern requires that there be a single ffx plugni that encapsulates all
arguments and subcommands.

See `args/` source directory or `$ ffx scrutiny verify --help` for more
high-level documentation.
