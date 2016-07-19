# Fuchsia prebuilts

Prebuilts are organized like this:

The file `prebuilt/manifest` contains a list of all the prebuilts we want to
download.  Each path in that file must exist in `prebuilt/versions`.

The files in `prebuilt/versions` contain version strings of their corresponding
tools.  The version strings are usually, but not required to be, the SHA of the
latest commit in the repo from which they were built.

The binaries are stored in Google Storage in paths that match the paths in
`prebuilt/versions`.  The filenames in Google Storage match the version strings.

## Example

`prebuilt/manifest` contains a line `magenta/qemu-arm64/magenta.elf`.

The file `prebuilt/versions/magenta/qemu-arm64/magenta.elf` exists and contains
the string `072a2f690d7ef8cc7116e9dbdf52a756b639c891`.

On Google Storage, there exists a filepath of
`gs://fuchsia-build/magenta/qemu-arm64/magenta.elf/072a2f690d7ef8cc7116e9dbdf52a756b639c891`
