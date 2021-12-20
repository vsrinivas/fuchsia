# Third party Source Management

Third party repositories are an important part of Fuchsia. Like any other piece
of software, our third party dependencies continue to evolve: add or remove
features, fix issues and potential vulnerabilities. Keeping our third party
dependencies and their build up-to-date is an important part of best security
practices.

## Overview

Third party repositories should prefer this layout (using `googletest` as an
example):

```
root [fuchsia.googlesource.com/fuchsia]
  third_party/
    googletest/
      BUILD.gn
      OWNERS
      README.fuchsia
      src/ [fuchsia.googlesource.com/third_party/github.com/google/googletest]
```

In this layout, keep `BUILD.gn`, `README.fuchsia` and `OWNERS` file (and
`config.h` file if present) directly inside [fuchsia.git][fuchsia_git]. The
third party repository would be checked out inside the `src` directory
corresponding to each dependency.

In this model, changing build flags for third party repositories can be done in
a single commit since `BUILD.gn` files live in fuchsia.git. When viable, third
party repositories can be pure mirrors of upstream repositories, so rolling
third party dependencies is simply a matter of updating the upstream revision,
if the content of `BUILD.gn` doesn't have to change.

## How to convert existing third party repositories?

### Soft transition

Soft transition is less disruptive and does not require a tree closure to land.
It is the generally preferred approach.

To perform a soft transition for `<name>` third party repository:

1.  Copy `BUILD.gn`, `README.fuchsia` and other Fuchsia-specific files from
    `//third_party/<name>` to `//build/secondary/third_party/<name>`.
2.  Update `//build/secondary/third_party/BUILD.gn` to use the new source
    location `//third_party/<name>/src`. This requires updating all sources,
    include directory paths, etc.
3.  Update the integration manifest replacing the existing forked project at
    `//third_party/<name>` with `//third_party/<name>/src`, while keeping
    revision unchanged. There shouldn't be any disruption as the build will now
    use the `BUILD.gn` file in secondary location.
4.  Move `BUILD.gn`, `README.fuchsia` and other Fuchsia-specific files from
    `//build/secondary/third_party/<name>` to `//third_party/<name>`.
5.  Change revision of `//third_party/<name>/src` to point to upstream.

### Hard transition

This approach is more disruptive (requires tree closure to land) than the soft
transition described above. Coordinate with build gardeners if you have to use
this approach.

To perform a hard transition for `<name>` third party repository:

1.  Prepare a change to copy `BUILD.gn`, `README.fuchsia` and other
    Fuchsia-specific files from `//third_party/<name>` to `//third_party/<name>`
    in fuchsia.git. We would also update `BUILD.gn` to refer to the new source
    location `//third_party/<name>/src` as part of the same change.
2.  Prepare a change to update the integration manifest replacing the existing
    forked project at `//third_party/<name>` with `//third_party/<name>/src`,
    while keeping revision unchanged.
3.  Submit all of these changes together during tree closure.
4.  Notify all developers to run `jiri update -gc` twice.
5.  Change revision of `//third_party/<name>/src` to point to upstream.

## Background

The most common changes made to our third party repositories are additions of
Fuchsia-specific files like `BUILD.gn`, `OWNERS` and `README.fuchsia`. In some
cases, there is a custom `config.h` file that's derived from the one generated
by the third party library build system. In the old model of managing third
party repositories, these Fuchsia-specific files are added to and tracked in
third party repositories directly.

New toolchains regularly introduce new warnings. Since Fuchsia build uses `-Wall
-Werror`, these new warnings are immediately applied to all code including third
party repositories. This is desirable since these warnings can find real issues.
However, third party repositories may not necessarily be clean, so we often need
to selectively disable these warnings on a per repository basis.

In the old model, this requires a separate `BUILD.gn` change and a roll for
every affected repository. In some cases, rolling out a new warning type may
require dozens of commits representing a significant toil for toolchain and
build teams.

An additional motivation is the ability to quickly address security
vulnerabilities that are disclosed in active upstreams. Currently the process is
somewhat tedious, slow and prone to errors due potentially breaking upstream
changes. Using pure mirrors would streamline the process, allowing to quickly
act on any newly published vulnerability. Note this does not necessarily apply
to repositories forked by Fuchsia.

When the repository needs to be updated, there are two options:

*   Rebase the change with our additions on top of the upstream changes and
    force push it back to the third party repository. To prevent garbage
    collection of the original change, which would now be disconnected from the
    graph and hence ready to be collected, we need to create a special branch to
    hold these changes. This strategy is error-prone and requires admin
    privileges. We also end up with a large number of branches whose sole
    purpose is to avoid garbage collection of past states.

*   Merge upstream changes in our third party repository on top of the change
    with our additions. This strategy avoids the need for creating backup
    branches and doesn't require administrative permissions. However, this
    strategy still requires manual effort. Furthermore, we would end up with a
    graph no longer resembling the upstream one, that is our repository will no
    longer have a linear history and the commit hashes will differ.

This page describes a new layout to replace the existing one. This new layout:

*   Streamlines the process of changing third party build flags.
*   Improves auditability of third party repositories.
*   Simplifies the process of updating third party repositories.

[fuchsia_git]: https://fuchsia.googlesource.com/fuchsia/+/refs/heads/main
