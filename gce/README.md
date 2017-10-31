# Fuchsia GCE Scripts

The scripts in this directory are convenience wrappers around primarily two
tools: `make-fuchsia-vol` that is a host tool in the build, and `gcloud` which
is the command like client for Google Cloud.

The scripts are usable via `//scripts/gce/gce` as an executable bash script.

## Prerequisites

 * You have followed the Fuchsia getting started documents: https://fuchsia.googlesource.com/docs/+/master/getting_started.md
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
fx set x86-64 --release
fx full-build
gce create-grub-image
gce create-fuchsia-image
gce create-instance
sleep 60
gce serial
```

## How the sausage is made

### gce/env.sh

The gce script suite comes with an `env.sh` that sets default environment
variables used by gce subcommands. The `gce` entrypoint script sources this.
Users can override all of the variables seen in `env.sh`, for example, altering
the name of the GCE instance you create/delete can be done by setting
`$FUCHSIA_GCE_INSTANCE`. See the contents of the script for more options.

### Grub and Fuchsia volumes

The approach these scripts use create two disks for your GCE instance. One
contains a minimal preconfigured GRUB bootloader that searches for, and boots,
a Zircon image in an EFI-style directory layout on any disk in the system.

The Fuchsia disk images are built as standard x86-64 EFI bootable GPT volumes.
The Grub image is mostly static and rarely needs to change. It can be shared
between members of the same cloud project.

When instances are created, the grub disk is used as a system image (this is
copied to the booting machine, and being small it facilitates fast boot times).
The Fuchsia image is attached as a new persistent disk.

### Command execution

The `gce` entrpoint script sources `env.sh`, then looks for a command script in
the `gce` scripts directory that matches the first argument given to it. If it
finds one, it shifts the first argument and execs that script.

## Commands

 * make-fuchsia-image - builds a local image file containing your local fuchsia
   build
 * create-fuchsia-image - builds and uploads a fuchsia image to GCE.
 * {make,create}-grub-image - builds grub and a grub boot disk image. The create
   script uploads that image to GCE. The grub image really only needs to be
   built once - it contains a grub configuration that searches for our standard
   EFI partition found in the images made by the `make-fuchsia-vol` tool.
 * serial - attaches to the instance serial port. Note that you will need to
   have an appropriately configured compute engine ssh key for this to work. If
   you have a more tuned ssh configuration, you may need to add
   `~/.ssh/google_compute_engine` to your `ssh-agent`.
