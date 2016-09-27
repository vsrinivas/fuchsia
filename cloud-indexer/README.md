# Modular Cloud Indexer

The Modular cloud indexer provides a simple interface for module developers to
upload their modules. We define the API as follows.

```
POST /api/upload
Content-Length: <content-length>
Content-Type: application/zip
Authorization: Bearer <oauth2-token>

<module-contents>
```

There are two points of note:
- First, the /api/upload endpoint is domain- and group- authenticated. To
facilitate domain-based authentication, we require that the client provides
the OAuth2 `userinfo.email` scope when issuing the request.
- Second, the module must be a zip archive with a manifest file `manifest.yaml`
at the root level. This manifest must contain valid `arch` and `modularRevision`
fields.

Upon successful upload, the module is surfaced through an index file. For a
given `arch` and `modularRevision`, the index file can be retrieved through an
authenticated GET request to the following endpoint:
<pre>
https://storage.googleapis.com/<b><i>bucket</i></b>/services/<b><i>arch</i></b>/<b><i>modularRevision</i></b>/index.json
</pre>

## Dependencies

First, install [Google Cloud SDK](https://cloud.google.com/sdk/). The SDK tools
are required to deploy the cloud indexer to a Google Cloud Platform project.
Installation instructions can be found
[here](https://cloud.google.com/sdk/downloads).

Next, install the [Dart SDK](https://www.dartlang.org/tools/sdk). We use the
Dart SDK at deployment to fetch dependencies using Pub. Installation
instructions can be found [here](https://www.dartlang.org/install).

After following the instructions, `gcloud`, `dart`, and `pub` should be
accessible through the path variable.

## Deployment

Local and cloud deployment options can be configured in the `config.json` file.

To deploy locally, we recommend using the
`{default,notification-handler}/bin/shelf.dart` executables to start each
service. We mock Pub/Sub push in the notification-handler service by setting
up an isolate that polls for notifications using a Pub/Sub pull subscription,
creating a request for each of the received messages.

Note that for local deployment, we assume that a service account key file is
accessible from `$HOME/.modular_cloud_indexer_key`. The JSON key file can be
downloaded from the `IAM & Admin > Service Accounts` panel in the Google Cloud
Console.

```sh
cd /path/to/cloud-indexer/app

# Start the default service at port 5024 without authentication. If a port
# number is not specified, the server starts at port 8080 by default.
dart default/bin/shelf.dart --disable-auth 5024

# In a different shell, start the notification-handler service at port 8000.
# Likewise, the server starts at port 8080 by default.
dart notification-handler/bin/shelf.dart 8000
```

To deploy remotely, we use `deploy.py` to package each service in a format
amenable to the `google/dart-runtime-base` Docker image before uploading to the
Google Cloud project.

```sh
cd /path/to/cloud-indexer
./deploy.py
```

The `--dry-run` option performs the deployment steps but stops short of
deploying using `gcloud`. Deployment usually occurs in a temporary folder. To
see the deployment output, we use the `--deploy-dir DEPLOY_DIR` option.
