# Fuchsia GCE Scripts

The scripts in this directory are convenience wrappers around primarily two
tools: `make-fuchsia-vol` that is a host tool in the build, and `gcloud` which
is the command like client for Google Cloud.

The scripts are usable via `//scripts/gce/gce` as an executable bash script.

## Prerequisites

 * You have followed the Fuchsia getting started documents: /docs/getting_started
 * You have installed & configured gcloud(1): https://cloud.google.com/sdk/gcloud/
 * You have setup defaults for gcloud, e.g.:
```
gcloud auth login
gcloud config set project my-awesome-cloud
gcloud config set compute/region us-west1
gcloud config set compute/zone us-west1-a
```

## Building and running an image

The following incantation will create the relevant disk images and boot a
Fuchsia instance:

```
cd $FUCHSIA_ROOT
fx set core.x64 --release
fx build
fx gce create-fuchsia-image
fx gce create-instance
sleep 60
fx gce serial
fx gce delete-instance
```

## How the sausage is made

### gce/env.sh

The gce script suite comes with an `env.sh` that sets default environment
variables used by gce subcommands. The `gce` entrypoint script sources this.
Users can override all of the variables seen in `env.sh`, for example, altering
the name of the GCE instance you create/delete can be done by setting
`$FUCHSIA_GCE_INSTANCE`. See the contents of the script for more options.

### Fuchsia volumes

The Fuchsia disk images are built as standard x86-64 EFI bootable GPT volumes.

### Command execution

The `gce` entrpoint script sources `env.sh`, then looks for a command script in
the `gce` scripts directory that matches the first argument given to it. If it
finds one, it shifts the first argument and execs that script.

## Commands

 * make-fuchsia-image - builds a local image file containing your local fuchsia
   build
 * create-fuchsia-image - builds and uploads a fuchsia image to GCE.
 * serial - attaches to the instance serial port. Note that you will need to
   have an appropriately configured compute engine ssh key for this to work. If
   you have a more tuned ssh configuration, you may need to add
   `~/.ssh/google_compute_engine` to your `ssh-agent`.
 * create-instance - create a GCE instance running fuchsia based on the most
   recently created fuchsia image.
 * delete-instance - deletes a GCE instance created by `create-instance`.
