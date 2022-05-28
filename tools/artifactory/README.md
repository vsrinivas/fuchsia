# Artifactory

Artifactory is a host tool which emits a GCS upload manifest of build artifacts
from the local build directory in an organized layout. The artifacts which are
written into the manifest are determined by build API metadata, e.g.
images.json, blobs.json, binaries.json, etc.

The infrastructure invokes artifactory and pipes the upload manifest into its
uploader after each build so that the artifacts may be accessed by downstream
clients.

## Cloud storage layout

Artifacts specific to a build e.g. images are uploaded to a unique namespace
according to a unique `-namespace`. Artifacts which may be shared across builds,
e.g. blobs, debug binaries, etc. are uploaded to a shared namespace. The precise
layout is documented in [cmd/up.go](cmd/up.go).

## Deduplication

Artifacts which live in shared namespaces are not uploaded more than once. Thus
the number of files uploaded, and the runtime of the tool, go down depending on
the amount of deduplication across builds and/or repeat invocations.
