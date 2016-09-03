Modular
=======

Modular is an experimental application platform.

It provides a post-API programming model. Rather than basing all
service calls on pre-negotiated contracts between known modules, the
system enables modules to
[call by meaning](http://www.vpri.org/pdf/tr2014003_callbymeaning.pdf),
specifying goals and semantic data types along side traditional typed
function inputs and outputs, and letting the system work out how those
semantic goals are satisfied and what services are employed.

**Setup, build and run** instructions for Linux / Android are in
[HACKING.md](https://fuchsia.googlesource.com/modular/+/master/HACKING.md).


**Setup, build and run** instructions for Fuchsia are
[here](https://fuchsia.googlesource.com/manifest/+/master/README.md). Once setup
is done, you can refer to the following cheatsheet for Modular:

```sh
export ROOT_DIR=/path/to/fuchsia/root

# Generate ninja files with modular autorun in bootfs. Modular autorun will
# launch story-manager.
$ROOT_DIR/packages/gn/gen.py -m modular-autorun
```

Alternatively, you can run the following bash script:

```sh
./apps/modular/fuchsia-scripts/build-and-run.sh
```
