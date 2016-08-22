# Modular Cloud Indexer

## Overview

The cloud indexer consists of two services:
- The first is a Python service that receives
[object change notifications](http://goo.gl/hNVOvx) from changes in the Modular
cloud storage. When the addition of a manifest file is registered, this service
sends a task to the indexing task queue.
- The task queue pushes to a Dart service that updates the appropriate indices
and writes them back to cloud storage. The task queue, as such, acts as a
concurrency mechanism to ensure that there is only a single writer to the index
files at any given time.

## Dependencies

Deploying the cloud indexer requires, on top of the dependencies bundled with
Modular, that you have the latest version of the
[Google Cloud SDK](https://cloud.google.com/sdk/) installed and that it is part
of the `PATH` variable.

## Deploy instructions

To deploy the cloud indexer, invoke `./deploy.sh` from the `cloud-indexer/`
directory. There are two additional flags that can be set.

- `--deploy-dir DEPLOY_DIR`, which takes the place of a temporary directory to
contain the deploy output; and
- `--dry-run`, which performs the deployment steps up to the actual deployment
to Google App Engine.

Of course, it doesn't make too much sense to invoke `--dry-run` without
`--deploy-dir`, as the output will simply be deleted.

The deployment directory of `notification-handler/` has a different structure to
that of the development directory. This is because the Docker image,
`google/dart-runtime-base`, requires that all local dependencies are within a
`pkg/` directory at deploy time. The script copies the dependencies and updates
the relative paths as necessary.

## Unit testing

### default service

Tests in the default service need to be invoked with the App Engine development
environment. You can do this by either installing
[NoseGAE](https://github.com/Trii/NoseGAE), or by writing or using a
[simple runner script](http://goo.gl/WtSQ3K).

### notification-handler service

Tests in the notification-handler services can be invoked directly using `pub`.

```sh
pub run test <PATH_TO_TEST>
```

## End-to-end testing

Running end-to-end tests require a Google Cloud project because `dev_appserver`
does not support Google Cloud Storage emulation. The following examples
demonstrate requests that would otherwise be sent as object change
notifications.

### Mock sync request

```sh
curl \
  -H 'X-Goog-Resource-State: sync' \
  -H 'X-Goog-Resource-Uri: <BUCKET_URI>' \
  -X POST <DEFAULT_SERVICE_URI>
```

### Mock exists request

In the case that a valid `services/<ARCH>/<REVISION>/<MANIFEST_NAME>.yaml`
object is provided as the name, the associated index will be updated.

```sh
curl \
  -H 'X-Goog-Resource-State: exists' \
  -H 'Content-Type: application/json' \
  -X POST \
  -d '{
      "name":"<OBJECT_NAME>",
      "bucket":"<BUCKET_NAME>"
  }' <DEFAULT_SERVICE_URI>
```
