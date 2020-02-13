# Owners

Each repository in the system has a set of owners. These are
tracked in files all aptly named `OWNERS`. One of these will
always be present in the root of a repository. Many directories will
have their own `OWNERS` files.

## Contents

Each file lists a number of individuals (via their email address) who
are familiar with and can provide code review for the contents of that
directory. We strive to always have at least two individuals in a
given file. Anything with just one is either too fine grained to
deserve its own, or is in need of someone else to learn enough about
the code to feel comfortable approving changes to it or answering
questions about it.

## Responsibilities

Fuchsia requires changes to have an OWNERS +2 review. However, many OWNERS files
contain a `*` allowing anyone to provide such a +2.

## Tools

INTK-108 tracks the work to stand up more infra tooling around these,
such as suggesting reviewers automatically in Gerrit.

## Format

We use the [Gerrit find-owners plugin][find-owners] file format for our
OWNERS files, with the addition of a comment indicating the default Monorail
component to use when filing issues related to the contents of this directory:

```
validuser1@google.com
validuser2@google.com

# COMPONENT: TopComponent>SubComponent
```

[find-owners]: https://gerrit.googlesource.com/plugins/find-owners/+/master/src/main/resources/Documentation/about.md
