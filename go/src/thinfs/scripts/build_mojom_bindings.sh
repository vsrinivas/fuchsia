#!/bin/bash
set -eu

# Generate mojom bindings first for thinfs...
cd $GOPATH/src/fuchsia.googlesource.com/thinfs/mojo
echo -n "Generating Mojoms from ThinFS's interfaces... "
$MOJO_DIR/src/mojo/public/tools/bindings/mojom_bindings_generator.py interfaces/filesystem/filesystem.mojom \
             --use_bundled_pylibs -I $MOJO_DIR/src -g go --no-generate-type-info -o $GOPATH/src
echo "Done"

# ... and then for mojo.
cd $MOJO_DIR/src
echo -n "Generating Mojoms from Mojo's interfaces... "

$MOJO_DIR/src/mojo/public/tools/bindings/mojom_tool.py gen \
  --output-dir $GOPATH/src \
  --src-root-path $MOJO_DIR/src \
  -I $MOJO_DIR/src \
  --generators go \
  --gen-arg no-go-src \
  mojo/public/interfaces/application/application.mojom \
  mojo/public/interfaces/bindings/service_describer.mojom \
  mojo/public/interfaces/bindings/mojom_types.mojom \

echo "Done"
