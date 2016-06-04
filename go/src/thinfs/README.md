# Thinfs is Not a File System #

thinfs is a lightweight disk management layer.

## Building ##

```shell
GOPATH=~/go
MOJO_DIR=~/mojo

# This assumes you already have depot_tools in your path.
cd $MOJO_DIR
fetch mojo
cd src
./build/install-build-deps.sh
mojo/tools/mojob.py gn
mojo/tools/mojob.py build --release

cd $GOPATH
mkdir -p src/fuchsia.googlesource.com
cd src/fuchsia.googlesource.com
git clone --recursive https://fuchsia.googlesource.com/thinfs

cd thinfs
# Fetch dependencies
go get -u github.com/Masterminds/glide
export PATH=$PATH:$GOPATH/bin/
glide install

# We need to patch the mojom bindings generator.  Find the line that says
#    self.Write(self.GenerateSource(), os.path.join("go", "src",
# and remove the "go" and "src" arguments.
# TODO(https://github.com/domokit/mojo/issues/768).
$EDITOR $MOJO_DIR/src/mojo/public/tools/bindings/generators/mojom_go_generator.py

# Generate mojom bindings first for thinfs...
cd $GOPATH/src/fuchsia.googlesource.com/thinfs/mojo
$MOJO_DIR/src/mojo/public/tools/bindings/mojom_bindings_generator.py interfaces/filesystem/filesystem.mojom \
             --use_bundled_pylibs -I $MOJO_DIR/src -g go --no-generate-type-info -o $GOPATH/src

# ... and then for mojo.
cd $MOJO_DIR/src
$MOJO_DIR/src/mojo/public/tools/bindings/mojom_bindings_generator.py \
             mojo/public/interfaces/application/application.mojom \
             --use_bundled_pylibs -I $MOJO_DIR/src -g go --no-generate-type-info -o $GOPATH/src
$MOJO_DIR/src/mojo/public/tools/bindings/mojom_bindings_generator.py \
             mojo/public/interfaces/bindings/service_describer.mojom \
             --use_bundled_pylibs -I $MOJO_DIR/src -g go --no-generate-type-info -o $GOPATH/src
$MOJO_DIR/src/mojo/public/tools/bindings/mojom_bindings_generator.py \
             mojo/public/interfaces/bindings/mojom_types.mojom \
             --use_bundled_pylibs -I $MOJO_DIR/src -g go --no-generate-type-info -o $GOPATH/src

# Build libext2fs
cd $GOPATH/src/fuchsia.googlesource.com/thinfs/lib/ext2fs
go generate

# Build the block device application.
cd $GOPATH/src/fuchsia.googlesource.com/thinfs/mojo/apps/blockd
GOPATH=$GOPATH:$MOJO_DIR CGO_CFLAGS=-I$MOJO_DIR/src \
             CGO_LDFLAGS="-L$MOJO_DIR/src/out/Release/obj/mojo -lsystem_thunks" go build \
            -tags 'fuchsia include_mojo_cgo' -buildmode=c-shared -o $MOJO_DIR/src/out/Release/blockd.mojo

# Build the filesystem service application.
cd $GOPATH/src/fuchsia.googlesource.com/thinfs/mojo/apps/filesystem
GOPATH=$GOPATH:$MOJO_DIR CGO_CFLAGS=-I$MOJO_DIR/src \
             CGO_LDFLAGS="-L$MOJO_DIR/src/out/Release/obj/mojo -lsystem_thunks" go build \
            -tags 'fuchsia include_mojo_cgo' -buildmode=c-shared -o $MOJO_DIR/src/out/Release/fs.mojo

# Build the filesystem client application.
cd $GOPATH/src/fuchsia.googlesource.com/thinfs/mojo/apps/fsclient
GOPATH=$GOPATH:$MOJO_DIR CGO_CFLAGS=-I$MOJO_DIR/src \
             CGO_LDFLAGS="-L$MOJO_DIR/src/out/Release/obj/mojo -lsystem_thunks" go build \
            -tags 'fuchsia include_mojo_cgo' -buildmode=c-shared -o $MOJO_DIR/src/out/Release/fsclient.mojo

# Make a test filesystem.
cd $MOJO_DIR/
dd if=/dev/zero of=test.fs bs=4096 count=16384
mkfs.ext2 $MOJO_DIR/test.fs

# Run the file system client application.
cd $MOJO_DIR/src
mojo/devtools/common/mojo_run --enable-multiprocess --release \
        --args-for="mojo:blockd -logtostderr -path $MOJO_DIR/test.fs -readonly=true" \
        --args-for="mojo:fs -logtostderr" --args-for="mojo:fsclient -logtostderr" mojo:fsclient

# If you see output that looks like
#
# I0603 16:43:45.294634    4996 main.go:78] Entry name=., type=Directory
# I0603 16:43:45.294969    4996 main.go:78] Entry name=.., type=Directory
# I0603 16:43:45.294996    4996 main.go:78] Entry name=lost+found, type=Directory
#
# then you know it worked.
```
