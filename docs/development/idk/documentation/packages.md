# Packages

A package is the unit of installation on a Fuchsia system.

## Anatomy

_To be added..._

## Working with packages

The majority of these instructions rely on the `pm` tool which is available
in `//tools`.

This document describes the various steps to build and install a package:

* [Build a package](#build-package)
* [Publish a package](#publish-package)
* [Install a package](#install-package)
* [Run a component from an installed package](#run-component)

For more details about each step, see `pm`'s help messages.

### Build a package {#build-package}

To build a package:

1. Create the package ID file:

   Note: `$PACKAGE_DIR` is a staging directory where the package
   is built.

   ```
   pm -o $PACKAGE_DIR -n $PACKAGE_NAME init
   ```

   This generates the package ID file implicitly as
   `$PACKAGE_DIR/meta/package`.  Set `$PACKAGE_ID_FILE` accordingly
   for use in subsequent steps:

   ```
   export PACKAGE_ID_FILE=${PACKAGE_DIR}/meta/package
   ```

   `$PACKAGE_ID_FILE` will contain the following data:

   ```
   {
     "name": "<package name>",
     "version": "<package version>"
   }
   ```

2. Create the manifest file, `$MANIFEST_FILE`, that provides the path to
   the package ID file.  Each line of a manifest file maps a single file that
   is contained in the package and is in the form of `destination=source` where:

   * `destination` is the path to the file in the final package
   * `source` is the path to the file on the host machine

   The manifest file must include at least one line for the package ID file like
   this:

   ```
   meta/package=<package ID file>
   ```

3. Generate the package metadata archive:

   ```
   pm -o $PACKAGE_DIR -m $MANIFEST_FILE build
   ```

   This creates the metadata archive at `$PACKAGE_DIR/meta.far`.

4. Create the package archive `$PACKAGE_ARCHIVE`:

   ```
   pm -o $PACKAGE_DIR -m $MANIFEST_FILE archive
   ```

   This command creates the package archive implicitly as
   `$PACKAGE_DIR/$PACKAGE_NAME-0.far`.  Set `$PACKAGE_ARCHIVE` accordingly
   for use in subsequent steps:

   ```
   export PACKAGE_ARCHIVE=${PACKAGE_DIR}/${PACKAGE_NAME}-0.far
   ```

   If the contents of the package change, you need to re-run the
   `pm -o $PACKAGE_DIR -m $MANIFEST_FILE archive` command.

You have successfully built a package. You are now ready to publish the package.

### Publish a package {#publish-package}

To publish a package:

1. Initialize a directory, `$REPO`, that serves as a packages repository:

   ```
   pm newrepo -repo $REPO
   ```

   This creates a directory structure named `$REPO` that is ready for
   publishing packages.

2. Publish packages to the repository `$REPO`:

   ```
   pm publish -a -r $REPO -f $PACKAGE_ARCHIVE
   ```

   `pm publish` parses `$PACKAGE_ARCHIVE` and publishes the package in the
   provided `$REPO` directory. If you run this command multiple times with
   different package archives, `pm publish` publishes the packages to the same
   repository. New versions of a same package can be published using the same
   command.

You have successfully published a package. You are now ready to install a
package.

### Install a package {#install-package}

To install a package:

1. Start the package server:

   ```
   pm serve -repo $REPO
   ```

   By default, this starts an amber server on the host machine at port `8083`.

2. (On the target device) Add the new repository as an update source with
   `amberctl`:

   ```
   amberctl add_repo_cfg -n $REPO -f http://$HOST_ADDRESS:8083/config.json
   ```

   If the component is not already on the system, `amberctl` installs the package.
   If the package already exists, `amberctl` installs any package updates.

You have successfully installed or updated the package. You are now ready to
run a component from the installed package.

### Run a component from an installed package {#run-component}

To run a component published in a package:

1. (On the target device) Run:

  Note: `$COMPONENT_URI` is in this form
  `fuchsia-pkg://${REPO}/${PACKAGE_NAME}#meta/<component name>.cmx`.

  ```
  run $COMPONENT_URI
  ```

You have successfully run a component from the installed package.

