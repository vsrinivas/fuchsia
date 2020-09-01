# Fuchsia Packages

A Fuchsia package is one or more collections of files that provide one or more
programs, components or services for a Fuchsia system. A Fuchsia package is a
term representing a unit of distribution, though unlike many other package
systems, that unit is composed of parts.

## meta.far

A package as "built" by the `pm` tool is a tree of zero or more
content-addressed items. At the top of this tree is a Fuchsia Archive
commonly named `meta.far`.

`meta.far` contains the `meta/` directory provided as an input to a package
build, and contains at minimum two files, described below. It can also
contain additional metadata items, such as component manifests.

meta/package
: The package identity file is a JSON file containing the name and version of
: the package.

meta/contents
: The contents file, typically produced automatically by `pm update` (an
: implied step in `pm build`) maps the user-facing file names of a package,
: to the content-addresses ([Merkle Root](/docs/concepts/packages/merkleroot.md))
: of those files.

The format of `meta/package` and `meta/contents` are considered private
specification at this time, and may be subject to change.

## Additional Metadata Items

It is common to store some additional files in the `meta` directory of a
package, for example [Component Manifests](/docs/concepts/components/v1/component_manifests.md).
