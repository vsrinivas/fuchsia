Scripts
=======================================

This repository is for scripts useful when hacking on Fuchsia. This repository should contain
scripts that perform tasks spanning multiple repositories. Scripts that only operate within a single
repository should live in the relevant repository.


# push-package.py

The push-package.py script pushes all of the files in a particular package.
No checking is performed for incremental changes.

The sample command lines below can be used to build the "modular" package and
then push it to the default device. This assumes you have already booted your
device with a version of Fuchsia that contains the most recent version of all
other packages.

```
fbuild apps/modular
scripts/push-package.py -o $FUCHSIA_BUILD_DIR packages/gn/modular
```

# fpublish

fpublish from env.sh will take a package from the build and create a Fuchsia
package manager [package](https://fuchsia.googlesource.com/pm/+/master/README.md#structure-of-a-fuchsia-package)
from a build package. It will then add the package to a local update respository
which, by default, is put at ${FUCHSIA_BUILD_DIR}/amber-files. It will also add
the package content files to the update repository and name these file after
their [Merkle Root](https://fuchsia.googlesource.com/docs/+/master/merkleroot.md).
If a package name is supplied to fpublish, only that package will be processed.
If no name is supplied, all the packages made by the build will be included.
