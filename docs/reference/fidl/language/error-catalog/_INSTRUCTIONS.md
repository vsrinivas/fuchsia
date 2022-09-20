# README

The purpose of this directory is to enable a cataloging of all `fidlc` errors.
Each markdown file in this directory represents the description for a unique
`fidlc` error.

Each error documented here should receive its own markdown file, with a prefix
of `_fi-` followed by the error's 4-digit domain-specific numeric code. Errors
should be paired with at least one success example (stored in the `/good`
directory) and at least one demonstration of the error case (stored in the
`/bad` directory). So, for error `fi-nnnn`, we should expect to find a markdown
file called `_fi-nnnn.md`, a success example at `/good/fi-nnnn.fidl`, and a
failure example at `/bad/fi-nnnn.fidl`.
