Scripts
=======================================

This repository is for scripts useful when hacking on Fuchsia. This repository
should contain scripts that perform tasks spanning multiple repositories.
Scripts that only operate within a single repository should live in the relevant
repository.


# push-package.py

The push-package.py script pushes the files listed in the given manifests files.
No checking is performed for incremental changes.

The sample command lines below can be used to build Modular and then push those
files to the default device. This assumes you have already booted your device
with a version of Fuchsia that contains the most recent version of all other
packages. This command line uses the "system_manifest" file from each of the
modular directories, such as modular, modular_dev, and modular_tests.

```
cd $FUCHSIA_DIR
fx build peridot:modular_all
scripts/push-package.py out/debug-x86-64/package/modular*/system_manifest
```

# fx publish

`fx publish` will take a package from the build and create a Fuchsia package
manager [package] from a build package. It will then add the package to a local
update respository which, by default, is put at
`${FUCHSIA_BUILD_DIR}/amber-files`. It will also add the package content files
to the update repository and name these file after their [Merkle Root].  If a
package name is supplied to `fx publish`, only that package will be processed.
If no name is supplied, all the packages made by the build will be included.

[package]: https://fuchsia.googlesource.com/pm/+/master/README.md#structure-of-a-fuchsia-package
[Merkle Root]: https://fuchsia.googlesource.com/docs/+/master/merkleroot.md
