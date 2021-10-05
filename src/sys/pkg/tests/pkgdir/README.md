# package-directory compatibility tests

The tests in this directory are meant to compare the [pkgfs] and
[package-directory] package directory implementations against each other to
ensure compatibility where possible and note the differences where they diverge
intentionally.

[old]: https://cs.opensource.google/fuchsia/fuchsia/+/main:src/sys/pkg/bin/pkgfs/pkgfs/
[new]: https://cs.opensource.google/fuchsia/fuchsia/+/main:src/sys/pkg/lib/package-directory/

Once pkgfs is deleted, these tests can be simplified and act as integration
tests of the package API surface.

## Intentional behavior differences

Because it's not practical to get the new behavior exactly identical, there will
be some behavior differences. The tests should call this out by making some
assertion logic conditional on `PackageSource::is_pkgfs()` or
`PackageSource::is_pkgdir()`. Any time this is done, it should link to a bug
blocking https://fxbug.dev/83755 or mention an entry in the following list:

### Hierarchical rights enforcement

package-directory implements hierarchical rights (because [vfs] does), and will
reject `Open()` calls which request rights which exceed the rights on the parent
connection.

[vfs]: https://cs.opensource.google/fuchsia/fuchsia/+/main:src/lib/storage/vfs/rust/

### `OPEN_RIGHT_ADMIN` not supported

package-directory categorically rejects opening packages with `OPEN_RIGHT_ADMIN`
in the first place, as those operations are not implemented for packages.

pkgfs allows but ignores `OPEN_RIGHT_ADMIN`.

### `OPEN_RIGHT_WRITABLE` not supported

package-directory categorically rejects opening packages with
`OPEN_RIGHT_WRITABLE` in the first place, as packages are immutable.

pkgfs generally rejects `OPEN_FLAG_WRITABLE`, but accepts it when opening the
root directory of a package.

### `OPEN_FLAG_CREATE` not supported

package-directory rejects opens with `OPEN_FLAG_CREATE` as packages are
immutable and so file creation is not supported.

pkgfs mostly also rejects this flag, but happens to accept it when re-opening
the package directory for some reason.

### `OPEN_FLAG_CREATE_IF_ABSENT` without `OPEN_FLAG_CREATE`

vfs (and therefore package-directory)
[rejects opens with `OPEN_FLAG_CREATE_IF_ABSENT` but not `OPEN_FLAG_CREATE`], as
that flag doesn't makes sense on its own.

In combination with "`OPEN_FLAG_CREATE` not supported", this means that
package-directory never allows `OPEN_FLAG_CREATE_IF_ABSENT`.

pkgfs is perfectly happy to accept `OPEN_FLAG_CREATE_IF_ABSENT` on its own
(although it does not actually let one create a file).

[rejects opens with `OPEN_FLAG_CREATE_IF_ABSENT` but not `OPEN_FLAG_CREATE`]: https://cs.opensource.google/fuchsia/fuchsia/+/main:src/lib/storage/vfs/rust/src/directory/common.rs;l=111-114;drc=84109901831eb08168fe3d2c3f9bcdc1844fd564

### `OPEN_FLAG_TRUNCATE` and `OPEN_FLAG_APPEND` not supported

package-directory rejects opens with `OPEN_FLAG_TRUNCATE` or `OPEN_FLAG_APPEND`
as those are only applicable to writing files and packages are immutable.

pkgfs mostly also rejects this flag, but happens to accept it when re-opening
the package directory for some reason.

### `OPEN_FLAG_DIRECTORY` enforced

package-directory rejects opening a file with `OPEN_FLAG_DIRECTORY`.
Additionally, it interprets paths with a trailing "/" as if they had set
`OPEN_FLAG_DIRECTORY` and prevents opening files with paths that contain a
trailing "/", [as per directory.fidl][trailing slash rule].

pkgfs rejects `OPEN_FLAG_DIRECTORY` when opening content files too, but does not
reject opening files in `meta/` with `OPEN_FLAG_DIRECTORY`. Additionally, it
allows opening files via paths with a trailing "/".

### `OPEN_FLAG_NOT_DIRECTORY` enforced

package-directory rejects opening a directory with `OPEN_FLAG_NOT_DIRECTORY`.

pkgfs ignores `OPEN_FLAG_NOT_DIRECTORY` and therefore allows opening files with
the flag.

### trailing slash implies `OPEN_FLAG_DIRECTORY`

package-directory allows opens of the form "subdir/" only if subdir is a
directory and not a file, [as per directory.fidl][trailing slash rule]:

> A trailing slash implies OPEN_FLAG_DIRECTORY.

pkgfs allows files to be opened via paths that contain trailing slashes.

[trailing slash rule]: https://cs.opensource.google/fuchsia/fuchsia/+/main:sdk/fidl/fuchsia.io/directory.fidl;l=190;drc=db4fbde2ad6f3872fe72dd5561265b88c0e0a7df

### `OPEN_FLAG_DIRECTORY` and `OPEN_FLAG_NOT_DIRECTORY` are mutually exclusive

package-directory rejects all opens with both flags set, even for meta which can
be opened both as a directory and a file.

pkgfs ignores `OPEN_FLAG_NOT_DIRECTORY` and therefore allows one to open meta as
a directory with `OPEN_FLAG_DIRECTORY | OPEN_FLAG_NOT_DIRECTORY`

### mode checked for consistency with `OPEN_FLAG{_NOT,}_DIRECTORY`

If mode and one of `OPEN_FLAG_DIRECTORY` or `OPEN_FLAG_NOT_DIRECTORY` is set,
package-directory checks them for consistency following the
[rules from fuchsia.io]:

> The mode type, if set, must always be consistent with the OPEN_FLAG_DIRECTORY
> and OPEN_FLAG_NOT_DIRECTORY flags, even if an object is not being created.

[rules from fuchsia.io]: https://cs.opensource.google/fuchsia/fuchsia/+/main:sdk/fidl/fuchsia.io/io.fidl;drc=87c8cef94d0de07a4d5d430589a0ec0bb9ae5950

pkgfs does not enforce any consistency between mode and these flags.

### mode is ignored other than consistency checking with `OPEN_FLAG{_NOT,}_DIRECTORY` and meta-as-file/meta-as-dir duality

package-directory ignores mode in open calls except for checking it for
consistency with `OPEN_FLAG{_NOT,}_DIRECTORY` (previous item) and its role in
determining if meta is opened as a file or directory.

pkgfs disallows `MODE_TYPE_DIRECTORY` when opening content files (but not meta
files).

### path segment rules are checked

package-directory enforces the following [rules for paths] defined in
fuchsia.io:

*   Components must not be empty (i.e. "foo//bar" is invalid).
*   ".." is disallowed anywhere in the path.
*   "." is only allowed if the path is exactly ".", but not otherwise.

[rules for paths]: https://cs.opensource.google/fuchsia/fuchsia/+/main:sdk/fidl/fuchsia.io/io.fidl;l=870-873;drc=db215d4621cda6044cb68e229c175162fe71b0b5

pkgfs collapses adjacent "//"s in the path, and allows extra "." segments.
Additionally, it will remove segments from paths preceding a ".." segment, so an
open of "subdir/../foo" will just result in an open of foo. (although note that
it does not allow one to escape the current directory handle this way)

### content files support `OPEN_FLAG_POSIX`

package-directory accepts `OPEN_FLAG_POSIX` when opening content files, although
it is ignored since the `OPEN_FLAG_POSIX*` family of flags only affect the
behavior of opening directories.

TODO(fxbug.dev/85062): figure out and document the situations where pkgfs
rejects it.

### meta/ directories and files may not be opened with `OPEN_RIGHT_EXECUTABLE`

package-directory rejects opening files and directories in meta/ with
OPEN_RIGHT_EXECUTABLE, as putting executable files in meta.far is not supported.

pkgfs has the same restrictions, except it allows one to open meta/ itself (as a
file or directory) with `OPEN_RIGHT_EXECUTABLE`, even though that does not
ultimately allow for the opening of files in meta/ with `OPEN_RIGHT_EXECUTABLE`.

### `GetToken()` not supported

package-directory responds with `ZX_STATUS_NOT_SUPPORTED` for calls to
`GetToken()`, because it represents an immutable directory and tokens are only
used with mutation APIs.

pkgfs hands out tokens via `GetToken()`, but they can't subsequently be used for
any other operation.
