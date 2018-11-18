# Fuchsia Build Tools

This repository contains the hashes of a number of prebuilt tools that are used
to build Fuchsia-related projects. The actual tools themselves are located in
Google Storage.

In most cases, the `jiri` tool will download the build tools automatically
during its `update` step. To download the tools manually, run `update.sh`.

## Uploading a tool

NOTE: These instructions are for Googlers only.

TODO: These instructions are out of date.  Use CIPD for everything.

### Installing gsutil

The tarballs are uploaded with the "gsutil" program.
See https://cloud.google.com/storage/docs/gsutil

There's a link there to download and install the Google Cloud SDK:
https://cloud.google.com/sdk/docs/

After installing the SDK you need to initialize/authenticate:
https://cloud.google.com/storage/docs/gsutil_install#authenticate

One of the steps will ask you for a cloud project. Choose loas-fuchsia-team.

At this point you can use gsutil to upload/download tarballs, view
cloud directory contents, and so on.

### Tarballs

Tarballs must have the tool name as the top level directory.
E.g.,

```
bash$ tar tvf qemu.tar.bz2
drwxr-xr-x ... qemu/
drwxr-xr-x ... qemu/libexec/
...
```

The uploaded file name is the sha1 hash of the tarball.
It could also be a sha1 hash of the tarball contents, avoiding
unnecessary spurious differences in uploads.

To compute the latter, one can do something like:

```
bash$ LC_ALL=POSIX cat $(find qemu/ -type f | sort) | shasum -a1
```

The sha1 hash is checked into the buildtools repo and supports
adding new tarballs or rolling back to a previous one.
See the *.sha1 files in buildtools/{mac,linux64}.

There are separate directories for mac and linux tarballs.
E.g.,

```
bash$ ./bin/gsutil ls gs://fuchsia-build/fuchsia/qemu/mac
gs://fuchsia-build/fuchsia/qemu/mac/
gs://fuchsia-build/fuchsia/qemu/mac/10d77d7df5b39440148ac3aab1a401ff42337a76
...
bash$ ./bin/gsutil ls gs://fuchsia-build/fuchsia/qemu/linux64
gs://fuchsia-build/fuchsia/qemu/linux64/
gs://fuchsia-build/fuchsia/qemu/linux64/10d77d7df5b39440148ac3aab1a401ff42337a76
...
```
