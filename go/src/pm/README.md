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
    - Signing packages
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
programs, components or services for a Fuchsia system. A published package is
signed by the original publisher. Additional concerns such as revocation,
attestations and distribution concerns are handled by the distribution system
separately from `pm`, see Amber for more details.

A package is defined by a set of metadata that are stored in a directory at the
top level of a Fuchsia package, as so:

```
meta/
  metadata
  contents
  signature
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

[Merkle Root](https://fuchsia.googlesource.com/docs/+/master/merkleroot.md)


### signature

The signature file is calculated from a cryptographic hash of all files in the
`meta/` directory except for `signature` itself. It must be signed using the
same key that is identified in the `metadata` file.

The signature algorithm is EdDSA. The message to be signed is constructed as follows:

* Glob all files in meta/ except for signature.
* Sort the names in byte order.
* Write all file names to the message.
* Write all file contents to the message.

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
not have strict version requirements. An exmaple might be fonts, where
additional font packages may be found and utilized by a font server. The
discovery of additional fonts.

TODO(raggi): improve documentation
