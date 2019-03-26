# Packages

A package is the unit of installation on a Fuchsia system.

## Anatomy

_To be added..._

## Building a package

The majority of this process relies on a tool called `pm` which is available
under `//tools`.
This document describes the various steps to generate a package. For more
details about each step, see `pm`'s help messages.

The initial step is to create a manifest file `$MANIFEST_FILE` describing the
contents of the package.
The manifest is a mere list of lines of the form `destination=source`, where
`source` is the path to the file on the host machine and `destination` the
location of that file in the final package.

The manifest must include at least one line for the package identity file:
```
meta/package=path/to/generated/package.json
```
This identity file should contain the following data:
```
{
  "name": "<package name",
  "version": "<package version>"
}
```
That file can be created using the `pm init` command.

From this point on, we are going to use `$PACKAGE_DIR` to denote a staging dir
where the package is going to be built.

First, we need to initialize the package with:
```
pm -o $PACKAGE_DIR -n $PACKAGE_NAME init
```

In order to create the package, a signing key is required. You may provide your
own key or generate one at `$SIGNING_KEY` with:
```
pm -k $SIGNING_KEY genkey
```
_TODO: add more details about signing keys, possibly in pm's help_

The next step is to generate an archive with the package's metadata:
```
pm -o $PACKAGE_DIR -k $SIGNING_KEY -m $MANIFEST_FILE build
```
This will create the metadata archive at `$PACKAGE_DIR/meta.far`.

Finally, we put it all together to generate the package itself:
```
pm -o $PACKAGE_DIR -k $SIGNING_KEY -m $MANIFEST_FILE archive
```
This will create the package archive at `$PACKAGE_DIR/$PACKAGE_NAME-0.far`.
Note that this step needs to be re-run if the contents of the package change.

## Deploying a package

### Publishing a package

First, initialize a directory that will serve as a packages repository:
```
pm newrepo -repo $REPO
```
This will create a directory structure at `$REPO` that is ready for
publishing packages.

The next step is to publish packages to that repository:
```
pm publish -a -r $REPO -f $PACKAGE_ARCHIVE.far
```
This will parse the provided package archive (`.far` file) and publish it in the
provided `$REPO` directory.

Running this command multiple times with different package archives will publish
those packages to the same repository. Similarly, new versions of a same package
can be published using the same command.

Finally, start the amber server with:
```
pm serve -repo $REPO
```
This will start an amber server on the host machine at port `8083` by default.

### Retrieving/Installing a package

_All commands in this section are executed on the target device._

First, add the new repository as an update source:
```
amberctl add_src -x -f http://$HOST_ADDRESS:8083/config.json
```

Then, run the component exposed by the package:
```
run $COMPONENT_URI
```
This will:
1. Install the package providing the component if not already in the system.
1. Check for updates to the package and install them if available.
1. Run the requested component.
