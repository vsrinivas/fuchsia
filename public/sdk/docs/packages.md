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
pm -o $PACKAGE_DIR -k $SIGNING_KEY genkey
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

_To be added..._
