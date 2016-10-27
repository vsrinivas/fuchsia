# Mojom Tool Binaries

On supported systems
(as of this writing only 64-bit Linux)
the appropriate architecture-specific subdirectory will contain a file named
_mojom_ after `gclient sync` is executed. The file _mojom_
is an executable built from (some version of) the Go code in
https://github.com/domokit/mojo/tree/master/mojom/mojom_tool.

The file _mojom_ is downloaded by `gclient sync` from Google Cloud
Storage from a file named _mojo/mojom_tool/`arch`/`sha1`_ where `arch`
is an architecture-specific folder name and `sha1` is the SHA1
digest of the file. Each architecture-specific subdirectory contains a
file named _mojom.sha1_, for example
[linux64/mojom.sha1](/mojom/mojom_tool/bin/linux64/mojom.sha1),
that specifies the SHA1 digest of the current version of the binary on that
architecture and lets `gclient sync`
decide whether or not the binary is already up-to-date.

To browse the Google Cloud Storage bucket go to
https://console.developers.google.com/storage/browser/mojo/mojom_parser/.

### Updating the File
To update the version of _mojom_ that will be downloaded by
`gclient sync,` see
https://github.com/domokit/mojo/blob/master/mojom/tools/build_mojom_tool.py.
