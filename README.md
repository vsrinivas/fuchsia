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
