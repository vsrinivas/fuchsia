# Packages

A package is the unit of installation on a Fuchsia system.

## Anatomy

_To be added..._

## Building a package

The majority of this process relies on a tool called `pm` which is available
under `//tools`.

This document describes the various steps to build a package. For more details about each step, see `pm`'s help messages.  We will use `$PACKAGE_DIR` to denote a staging dir where the package is going to be built.

First, **(1)** create the package ID file:
```
pm -o $PACKAGE_DIR -n $PACKAGE_NAME init
```

The package ID file will be generated as `$PACKAGE_DIR/meta/package` and will contain the following data:
```
{
  "name": "<package name>",
  "version": "<package version>"
}
```

The next step is to **(2)** create a manifest file `$MANIFEST_FILE` that will provide the path to our new package ID file.  Each line of a manifest file maps a single file that will be contained in the package and is in the form `destination=source` where:
* `destination` is the path to the file in the final package
* `source` is the path to the file on the host machine

The manifest file must include at least one line for the package ID file like
this:
```
meta/package=<package ID file>
```

The next step is to **(3)** generate the package metadata archive:
```
pm -o $PACKAGE_DIR -m $MANIFEST_FILE build
```
This will create the metadata archive at `$PACKAGE_DIR/meta.far`.  Next, **(4)** create the package archive `$PACKAGE_ARCHIVE`:
```
pm -o $PACKAGE_DIR -m $MANIFEST_FILE archive
```

This will create the package archive as `$PACKAGE_DIR/$PACKAGE_NAME-0.far`.  Note that this step needs to be re-run if the contents of the package change.

## Publishing a package

**(5)** initialize a directory, $REPO, that will serve as a packages repository:
```
pm newrepo -repo $REPO
```
This will create a directory structure at `$REPO` that is ready for publishing packages.  The next step is to **(6)** publish packages to that repository:
```
pm publish -a -r $REPO -f $PACKAGE_ARCHIVE
```

`pm publish` will parse `$PACKAGE_ARCHIVE` and publish it in the provided `$REPO` directory.

Running this command multiple times with different package archives will publish those packages to the same repository.  Similarly, new versions of a same package can be published using the same command.

## Installing a package

**(7)** start the package server with:
```
pm serve -repo $REPO
```

This will start an amber server on the host machine at port `8083` by default.  Now, **on the target device** **(8)** add the new repository as an update source run `amberctl`:
```
amberctl add_repo_config -n $REPO -f http://$HOST_ADDRESS:8083/config.json
```

## Running a component from an installed package
To run a component published in a package, **on the target device** **(9)** run the following command:
```
run $COMPONENT_URI
```

where `$COMPONENT_URI` is of the form:
```
fuchsia-pkg://${REPO}/${PACKAGE_NAME}#meta/<component name>.cmx
```

The preceding installation and running steps will:
1. Install the package providing the component if it is not already on the system.
1. Check for updates to the package and install them if available.
1. Run the requested component.

