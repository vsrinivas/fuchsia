# pm - The Fuchsia Package Manager

The Fuchsia Package Manager is responsible for controlling and (locally) serving
packages on a Fuchsia system. It is not responsible for indexing, updates or
post-fetch operations that are handled by other systems.

`pm` is also a command line tool that can be used to perform some package
related operations.

## pm

PM is the package manager command line interface. It is responsible for:

 * Demonstrating and exercising the `pmd` APIs.
 * Enabling developer oriented package functions such as:
    - Archiving packages
    - Installing packages
    - Listing packages
    - Removing packages
    - Validating packages
 * Summarizing the interface to Amber (?)

PM runs on several operating systems, although only a subset of operations are
supported on platforms that do not have PMD.

## pmd

PMD is the "Package Manager Daemon" and is responsible for:

 * Activating packages (making metadata locally available)
 * Serving packages (providing filesystem type access to packages)
 * Management (garbage collection of unused / unwanted packages)
 * Enumeration (seeing what packages and contents are locally available)

PMD only runs on Fuchsia.

## Structure of a Fuchsia Package

A Fuchsia package is one or more collections of files that provide one or more
programs, components or services for a Fuchsia system. Additional concerns
such as revocation, attestations and distribution concerns are handled by the
distribution system separately from `pm`, see Amber for more details.

A package is defined by a set of metadata that are stored in a directory at the
top level of a Fuchsia package, as so:

```
meta/
  package
  contents
```

Additional files may extend the specification in the future.

### metadata

The metadata file contains a basic set of system, developer and user friendly
data. Examples include human name, unique name, version, developer name,
developer public key, and description.

TODO(raggi): provide exact format and structure of first gen metadata.

### contents

The contents file contains a strict and complete manifest of all files that
are in the entire package. The names double as paths for packaging and paths
for presentation at runtime. Associated with each path is a hash of the
content using the merkle root strategy documented as below.

[Merkle Root](/docs/concepts/storage/merkleroot)


## Conventions for Fuchsia Packages

The Fuchsia Package System does not provide native support for inter-package
dependencies. This is a considered restriction in order to avoid various
challenges that are inherent to dependency management and versioning. This
choice keeps the system extremely simple and secure. The solution for handling
dependencies is therefore to embed all dependencies into a package. The
features of the package manager, the package distribution and the Fuchsia
storage systems then perform various levels of deduplication in order to make
this approach transport, storage and runtime efficient.

Versions in the Fuchsia package system are open ended, but are largely
considered to be linear. This is most relevant when integrating a third party
package system such as Rubygems into the Fuchsia package system. By convention
third party packages are integrated by adding the third party package version as
a specific Fuchsia package. Concretely, third party package `foo` version
`1.2.3` will conventionally be packaged as a Fuchsia package with the name
`foo-1.2.3`. There may be additional naming conventions for language/runtime
specific names in future. The goal is that if `foo` had contained a machine
specific binary blob, the package can be rebuilt and subsequently updated
without altering the "version" from the perspective of a higher level package
manager.

Data dependencies and optional dependencies may be additional packages, but due
to the explicit lack of a mechanism to manage specific versions of such things,
it is expected that those cases will not be shared library objects. Instead they
should be limited to progressive enhancement and safely loaded things that do
not have strict version requirements. An example might be fonts, where
additional font packages may be found and utilized by a font server. The
discovery of additional fonts.

TODO(raggi): improve documentation

## Updating Fuchsia Packages

When defining the contents of a Fuchsia package, it is worth considering how
that package will be updated to a newer version. Essentially, to update to a
newer version of a package, a Fuchsia system must determine which blobs
referenced by the new package's `meta/contents` file are not present on the
system and download them. Likewise, after updating a package, any blobs that are
no longer referenced in the package's `meta/contents` may be safely removed from
the system, provided that no other package on the system references those blobs.

A major implication of this update model is that updates are performed at
file granularity. If a Fuchsia package contains a single file with many assets,
for example, updating a single asset within that file will require downloading
the entire file. In this case, including the package assets as individual files
would allow an update to download just the assets that have changed. This
concept applies to binaries as well. To optimize package update download sizes,
prefer dynamically linking to libraries instead of statically linking,
especially if those dependencies change infrequently.

It is difficult to know how changes to a Fuchsia package will affect its update
download size. Fortunately, `pm` has a few subcommands to help.

### Package Snapshots

A Package Snapshot contains package and file metadata from a set of Fuchsia
packages, and two package snapshots can be compared to simulate updating from
one snapshot of packages to another.

Within the Platform Source Tree, a build automatically produces a package
snapshot of all products and packages enabled by `fx set`. The automatically
generated snapshot is stored in the output directory at
`obj/build/images/system.snapshot`. Outside of the Platform Source Tree,
snapshots can be built from a set of packages using `fx snapshot`.

To manually produce a package snapshot file:
1. When building a Fuchsia package with `pm build`, pass in an `-blobsfile`
   argument to also generate a metadata file of all files within the package.

   Sample package metadata files are located at
   `examples/snapshots/(source|target)/pkg_*.json`
2. Merge one or more package metadata files into a single package snapshot using
   `pm snapshot`. Packages within a snapshot can be tagged with arbitrary
   strings. When comparing two snapshots, these tags can be used to select or
   exclude certain packages.

   Using the package metadata files under `examples/snapshots/source` as an
   example:
   ```sh
   pm snapshot \
       -package "a=examples/snapshots/source/pkg_a.json" \
       -package "b#optional=examples/snapshots/source/pkg_b.json" \
       -output "examples/snapshots/source/packages.snapshot"
   ```

   Or,

   Create a manifest file containing a package entry per line. Then, provide the
   manifest to `pm snapshot`:
   ```sh
   pm snapshot \
       -manifest "examples/snapshots/source/packages.manifest" \
       -output "examples/snapshots/source/packages.snapshot"
   ```


The contents of the package snapshot for the packages at
`examples/snapshots/source/pkg_*.json` are included below. Notice how the
package entries list a total of 7 files, but the blobs list only contains 6
files. The blob for `lib/ld.so.1` is shared between `a` and `b`.

```json
{
  "packages": {
    "a": {
      "files": {
        "bin/app": "aa02000000000000000000000000000000000000000000000000000000000000",
        "img/logo.png": "aa03000000000000000000000000000000000000000000000000000000000000",
        "lib/ld.so.1": "ab00000000000000000000000000000000000000000000000000000000000000",
        "meta/": "aa01000000000000000000000000000000000000000000000000000000000000"
      }
    },
    "b": {
      "files": {
        "bin/app": "bb02000000000000000000000000000000000000000000000000000000000000",
        "lib/ld.so.1": "ab00000000000000000000000000000000000000000000000000000000000000",
        "meta/": "bb01000000000000000000000000000000000000000000000000000000000000"
      },
      "tags": [
        "optional"
      ]
    }
  },
  "blobs": {
    "aa01000000000000000000000000000000000000000000000000000000000000": {
      "size": 20480
    },
    "aa02000000000000000000000000000000000000000000000000000000000000": {
      "size": 2686144
    },
    "aa03000000000000000000000000000000000000000000000000000000000000": {
      "size": 1000000
    },
    "ab00000000000000000000000000000000000000000000000000000000000000": {
      "size": 846896
    },
    "bb01000000000000000000000000000000000000000000000000000000000000": {
      "size": 20480
    },
    "bb02000000000000000000000000000000000000000000000000000000000000": {
      "size": 2686144
    }
  }
}
```

Using this package snapshot as a baseline, we can now compare it to another
package snapshot (target) to show what needs to happen to update from `source` to
`target`.

```
pm delta examples/snapshot/source/packages.snapshot examples/snapshot/target/packages.snapshot
```

Which produces the following output:
```
Source size: 6.9 MiB
Target size: 8.8 MiB
Discard size: 3.6 MiB
Keep size: 3.4 MiB
Download size: 5.5 MiB

Top 2 packages with largest update size:

Discard  Keep     Download  Name
2.6 MiB  827 KiB  4.5 MiB   b
                  4.5 MiB   - bin/app
                  20 KiB    - meta/
997 KiB  3.4 MiB  997 KiB   a
                  977 KiB   - img/logo.png
                  20 KiB    - meta/

Top 4 largest new blobs:

Download  Name
4.5 MiB   b/bin/app
977 KiB   a/img/logo.png
20 KiB    a/meta/
20 KiB    b/meta/
```

In the above example, `b`'s application binary would be replaced with a new
version, and `a`'s logo asset would be updated as well. When a package is
modified in any way, its meta FAR (represented by the "meta/" directory) would
also change. "Discard size" represents the total size of all blobs that can be
garbage collected after the update completes. "Keep size" represents the total
size of all blobs that continue to be referenced by one or more packages after
the update. Finally, "Download size" represents the total size of all blobs that
need to be downloaded to complete the update.

Note that the total download size is not always the sum of each package's
download size. If a blob that is shared between multiple packages (or even
referenced more than once within a single package) is updated, it only needs to
be downloaded once but will appear as an update to each package that references
the blob. Similar concepts apply to the discard and keep sizes.

`pm delta` has various options to customize its output. See `pm delta --help`
for more info. For example, `pm delta` can output its statistics in JSON format.
This output format is intended to be parsed by tooling for further processing
or rendering. An example of this output is at
[`examples/snapshots/delta.json`](examples/snapshots/delta.json).
