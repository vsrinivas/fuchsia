# Fuchsia package URLs

Fuchsia software, including system components and third-party components, is
distributed in the form of packages.  These packages are signed in such a way
as to produce a cryptographic chain of trust between the repository root and
the package itself.  Fuchsia relies on this chain of trust to ensure the
authenticity of packages as required to implement features such as verified
boot.

This document addresses the problem of how to identify individual packages
using **fuchsia-pkg** URLs.

## Goals

 * Encode stable identifiers for packages in the form of a URL under the
   assumption that many system components will effectively "bookmark" URLs for
   the long term.  (Example: to persist the URLs of all modules in a story.)
 * Establish an association between a package URL and the hostname of an
   Internet server from which the package could perhaps be downloaded.
 * Be robust in the face of repository mirrors: a package's authenticity can be
   proven even if the contents were actually downloaded from a different host
   than the one specified in the package URL.  (Example: edge cache,
   peer-to-peer, USB stick...)
 * Be relatively human-readable.
 * Be strict about representation, including structure, allowed characters, and
   maximal length.
 * No requirement for TLS at point of distribution: the package and repository
   metadata and auxiliary information about known sources is enough to verify
   authenticity independently of how the contents were actually obtained given a
   package URL (even offline).

## Non-goals

 * Establish a cryptographically strong association between a package URL itself
   and its repository's chain of trust, enabling proof of authenticity given
   nothing but the URL.

## Identifying repositories, packages, and resources

This section describes the various characteristics used to identity
repositories, packages and resources.

These definitions have been chosen to align with the [TUF Specification] where
possible.

These identifying characteristics are not intended to be shown to end-users
during normal operation (exception: developers and system administrators).
Consequently, we may eschew concerns related to localization of names.

[TUF Specification]: https://github.com/theupdateframework/specification/blob/master/tuf-spec.md#4-document-formats

## Repository identity

### Repository root verification (known sources)

The repository's root role (a quorum of one or more public/private key pairs)
establishes a chain of trust such that package authenticity, integrity, and
freshness can be verified cryptographically.  The root role signs keys for more
limited roles which are then used to sign package metadata and the targets
themselves. See [here][TUF Security] and [here][TUF METADATA] for more details.

To verify that a package is authentic, we must also verify that the repository
from which it is being downloaded is authentic.  This will be implemented by
maintaining a list of known source repositories with their public keys on the
device.  Packages from unknown sources will be rejected.

[TUF Security]: https://theupdateframework.github.io/security.html
[TUF Metadata]: https://theupdateframework.github.io/metadata.html

### Repository hostname

The package URL contains a repository [hostname] to identify the package's
source.  Per [RFC 1123] and [RFC 5890], a hostname is a sequence of dot
(`.`)-delimited [IDNA A-labels], each of which consists of 1 to 63 of the
following latin-1 characters in any order: digits (`0` to `9`), lower-case
letters (`a` to `z`), or hyphen (`-`).  No other characters are permitted.  The
total maximum length of a hostname is 253 characters including the dots.

[hostname]: https://en.wikipedia.org/wiki/Hostname
[RFC 1123]: https://tools.ietf.org/html/rfc1123
[RFC 5890]: https://tools.ietf.org/html/rfc5890
[IDNA A-labels]: https://tools.ietf.org/html/rfc5890#section-2.3.2.1

**Example repository hostnames:**

 * `fuchsia.com`
 * `mycorp.com`

## Package identity

### Package name {#package-name}

A package name is a symbolic label which identifies a logical collection of
software artifacts (files), independent of any particular variant or revision
of those artifacts.  The package name is used to locate package metadata within
a repository.  Package metadata must be signed by a role which is trusted by
the repository root.

A package name consists of a sequence of up to 100 of the following latin-1
characters in any order: digits (`0` to `9`), lower-case letters (`a` to `z`),
hyphen (`-`), and period (`.`).  No other characters are permitted.

A package's name must be unique among all packages in a repository.
Conversely, packages within different repositories are considered distinct even
if they have the same name.

**Examples of package names:**

 * `fuchsia-shell-utils`
 * `fuchsia-scenic`
 * `fuchsia-fonts`
 * `mycorp-product`

### Package variant

Note: Package variants are considered deprecated and you should not rely on
them at this time. For the purposes of almost all operations, if a variant
is required, it should be specified as `0`.

A package variant is a symbolic label for a sequence of package updates.
Different variants of the same package may receive different updates, at
different rates, and/or with different content.  The package variant is used to
locate metadata for a sequence of package updates within a repository.  Variant
metadata must be signed by a role which is trusted by the role which signed the
package's metadata.

A package variant consists of a sequence of up to 100 of the following latin-1
characters in any order: digits (`0` to `9`), lower-case letters (`a` to `z`),
hyphen (`-`), and period (`.`).  No other characters are permitted.

What a package variant actually represents is at the discretion of the software
developer and/or distributor responsible for the package since they control the
sequence of updates.

**Example package variant conventions:**

 * **update channels**: `stable`, `beta`, `bleeding-edge`, ...
 * **major product upgrades**: `antelope`, `bear`, `caterpillar`, `deer`, …
 * **a combination of the above**: `antelope-stable`, `deer-beta`, …
 * **breaking API revisions**: `1.0`, `1.1`, `2.0`, …
 * **variant-free**: `default`

This two-level scheme of package name and variant increases the overall
flexibility of the package identification system.

### Package hash

A package hash is the [merkleroot] of the package's meta.far.  Because the
package's metadata encodes the content addresses of the package's files, any
changes to the package's metadata or content will produce a different package
hash, thereby making it possible to distinguish each unique revision of the
package.

A package hash is represented as a hex-encoded string consisting of exactly 64
of the following latin-1 characters: digits (`0` to `9`) and lower-case letters
(`a` to `f`).  No other characters are permitted.

**Example package hashes:**

 * `15ec7bf0b50732b49f8228e07d24365338f9e3ab994b00af08e5a3bffe55fd8b`

[merkleroot]: /docs/concepts/packages/merkleroot.md

## Resource identity

### Resource paths {#resource-paths}

A resource path is a UTF-8 string which identifies a resource within a package.
This is a file path, consisting of a sequence of single `/` delimited
path segments, each of which is a non-empty sequence of non-zero UTF-8
characters not equal to `.`, `..`, or `/`.

This definition is compatible with the definition of [Fuchsia filesystem paths]
but it imposes a UTF-8 encoding rather than admitting arbitrary binary strings
since such strings cannot always be encoded as valid URLs.

Per [RFC 3986], resource paths are percent-encoded when they appear in URLs.

**Example resource paths:**

 * `meta/my.component`
 * `bin/myprogram`
 * `lib/mylibrary.so`
 * `assets/en/strings`
 * `hello/unicode/%F0%9F%98%81`, which decodes to `hello/unicode/😁`

[Fuchsia filesystem paths]: /docs/concepts/framework/namespaces.md#object-relative-path-expressions
[RFC 3986]: https://tools.ietf.org/html/rfc3986#page-11

## The fuchsia-pkg URL scheme

The **fuchsia-pkg** URL scheme combines the preceding identifying
characteristics to establish a means for referring to a repository, a package,
or a resource, depending on which parts are included.

## Syntax

```
fuchsia-pkg://<repo-hostname>[/<pkg-name>[/<pkg-variant>][?hash=<pkg-hash>][#<resource-path>]]
```

**Scheme: (required)**

 * The following case-insensitive characters: `fuchsia-pkg://`.
  * Although the canonical form is lower-case, URL scheme encoding is
    case-insensitive therefore the system must handle all cases.

**Repository: (required)**

 * The repository hostname encoded as dot-delimited IDNA A-Labels.

**Package: (optional)**

 * A single `/` character.
 * The [package name](#package-name).
 * Optionally followed by...
   * A single `/` character.
   * The package variant.

**Package Hash: (optional, only valid if a package was specified)**

 * The string `?hash=`.
 * The [package hash](#package-hash).

**Resource Path: (optional, only valid if a package was specified)**

 * A single `#` character.
 * The UTF-8 [resource path](#resource-paths), relative to the root of the
   package, percent-encoded as required, per [RFC 3986].

URL components containing reserved characters are percent-encoded according to
[RFC 3986].  Note that the scheme, [repository hostname](#repository-hostname),
[package name](#package-name), [package variant](#package-variant), and [package
hash](#package-hash) components are all defined to use a restricted subset of
characters, none of which require encoding, unlike the resource path.

## Interpretation

A **fuchsia-pkg** URL has different interpretations depending on which parts are
present.

 * If the repository, package, and resource parts are present, then the URL
   identifies the indicated resource within the package.
 * If only the repository and package parts are present, then the URL identifies
   the indicated package itself.
 * If only the repository parts are present, then the URL identifies the
   indicated repository itself.

The package parts can express varying degrees of specificity.  At minimum the
package name must be present, optionally followed by the package variant and
package hash.

When the **package resolver** fetches resources given a **fuchsia-pkg** URL, it
is required that the package variant be specified. If the package hash is
missing, the **package resolver** fetches the resources from the newest revision
of the package variant available to the client.

Although a repository hostname is included in the URL, it is safe to fetch
resources from any replica of the repository which satisfies the same
cryptographic chain of trust.  The problem of locating an appropriate mirror is
beyond the scope of this document.

**Examples**

 * a repository:
   * `fuchsia-pkg://fuchsia.com`
 * a package:
   * `fuchsia-pkg://fuchsia.com/fuchsia-shell-utils`
   * `fuchsia-pkg://fuchsia.com/fuchsia-shell-utils/stable`
 * a resource:
   * `fuchsia-pkg://fuchsia.com/fuchsia-shell-utils/stable#bin/ls`
   * `fuchsia-pkg://google.com/chrome/stable#meta/webview.component`
 * a resource from a specific hashed revision of a package:
   * `fuchsia-pkg://google.com/chrome/stable?hash=80e8721f4eba5437c8b6e1604f6ee384f42aed2b6dfbfd0b616a864839cd7b4a#meta/webview.component`
