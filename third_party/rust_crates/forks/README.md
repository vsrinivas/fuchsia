# Forked external Rust crates

This directory contains crates which are vendored in order to be modified locally. Each crate must
have a `README.fuchsia` markdown file in its root which answers these questions:

* What is this crate used for?
* Are there any use restrictions? i.e. only for development hosts
* What differs from upstream? Include a changelog if feasible.
* Are there any restrictions to how it should be rolled?
* Is there anything else which makes this dependency "special"?
