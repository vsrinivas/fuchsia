## Generated LLCPP code for unit tests

These files (`fidl_llcpp_basic.cpp`, `fidl_llcpp_basic.h`) are checked in for the time being, due to
the Golang-based `fidlgen` not accessible from Zircon yet. The command to generate these files are:

```bash
# assuming garnet and zircon are in the same parent directory,
# first go to a garnet branch with the llcpp implementation
cd garnet

# trigger building of fidlgen, don't care if test passes
fx run-host-tests fidlgen_golang_test || true

# obtain a path to fidlc and fidlgen; alternatively, specify them manually
cd go/src/fidl/compiler/backend/typestest
EXAMPLE_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
FUCHSIA_DIR="$( echo ${EXAMPLE_DIR} | sed -e 's,garnet/go/src.*$,,' )"
FIDLC=${FUCHSIA_DIR}out/x64/host_x64/fidlc
FIDLGEN=${FUCHSIA_DIR}out/x64/host_x64/fidlgen

# generate the json IR
cd ${FUCHSIA_DIR}zircon/system/utest/fidl-llcpp-interop
${FIDLC} --json /tmp/basictypes.fidl.json \
         --files ./basictypes.fidl

# generate llcpp bindings
${FIDLGEN} -generators llcpp \
           -json /tmp/basictypes.fidl.json \
           -output-base fidl_llcpp_basic \
           -include-base .

# move bindings to the `generated` directory
mv fidl_llcpp_basic.h generated
mv fidl_llcpp_basic.cpp generated

# cleanup
rm /tmp/basictypes.fidl.json
```

Whenever the llcpp codegen in Garnet is updated, these files should be re-generated and checked in.

As soon as the merger between the Garnet repo and Zircon happens, we should modify the build system
to automatically generate llcpp code, at which point we can remove this directory.
TODO(FIDL-427): replace manual code generation with automated solution.
