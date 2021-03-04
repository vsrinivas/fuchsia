# Build system policies

This document details design principles and specific technical decisions made
that relate to how the Fuchsia build should work.
These principles apply to all modes of using the Fuchsia build, for instance
whether by interactive engineering workflows or via automated systems such as
CI/CQ.

## Goals and priorities of the build

Like any system, the build is often subject to multiple conflicting
requirements. When there is a conflict, we generally seek to satisfy these
priorities by order of importance:

1. Meet customer requirements, as determined by Fuchsia technical leadership.
2. Ensure correctness: produce the desired outputs.
3. Promote maintainability: documentation, sound engineering processes.
4. Improve performance: perform the same builds at a lower cost.

## Desired properties of the build

The following are considered to be good properties for the build:

* Hermeticity - the build is self-contained and neither influences external
  software and configuration or is influenced by external software and
  configuration. Build steps 
* Repeatability and reproducibility - two builds from the same source tree
  produce the same output or the same outcome deterministically.
  Reproducibility promotes security and auditing, and simplifies
  troubleshooting.
* Efficient - builds should only spend time doing work relevant to the build,
  and must aim to minimize the impact on both human and infrastructure costs.
* Portability - builds should produce consistent results across all supported
  host platforms.

These are ideals.
We aim to meet these ideals and measure our progress against these measures.

## Python scripts as build actions

Python scripts may be used as build actions.

Please follow the [Google style guide for Python][python-style].

Fuchsia currently uses Python 3.8. All Python sources are to begin with the
following:

```shell
#!/usr/bin/env python3.8
```

## Shell scripts as build actions

Shell scripts may be used as build actions.

Shell scripts are encouraged for tasks that can be expressed with a few simple
shell commands. For complex operations, other languages are preferred.

Please follow the [Google style guide for shell scripting][bash-style].
Please use [shellcheck] to find and correct common shell programming errors.

We prefer POSIX (aka Bourne) shell scripts for portability across wide set of
host platforms.
If you're maintaining an existing Bash script, please restrict the features
used to version 3.2, or consider rewriting the script as POSIX shell script.
To check whether your script is POSIX compliant, you can use:

```posix-terminal
shellcheck --shell=sh
```

Scripts that run on POSIX shell should begin with the following:

```shell
#!/bin/sh
```

Scripts that specifically require Bash should begin with the following:

```shell
#!/bin/bash
```

[bash-style]: https://google.github.io/styleguide/shellguide.html
[python-style]: https://google.github.io/styleguide/pyguide.html
[shellcheck]: https://www.shellcheck.net/
