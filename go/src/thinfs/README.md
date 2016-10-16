# Thinfs

Thinfs is a collection of disk managment utilities which can be used to build a
filesystem.

Thinfs currently includes an implementation of:
 * FAT16 and FAT32

## Download Sources ##

### Download ThinFS sources ###
```shell
jiri import thinfs
jiri update
cd $FUCHSIA_ROOT/go/src/fuchsia.googlesource.com/thinfs
```

### Download Mojo sources ###

NOTE: At the moment, using ThinFS with Mojo is not recommended. Currently,
ThinFS is only compatible with the Remote IO protocol within Magenta.

```shell
# This path is customizable, but it MUST be set.
MOJO_DIR=~/mojo

# This assumes you already have depot_tools in your path.
cd $MOJO_DIR
fetch mojo
cd src
./build/install-build-deps.sh
mojo/tools/mojob.py gn
mojo/tools/mojob.py build --release

# We need to patch the mojom bindings generator.  Find the line that says
#    self.Write(self.GenerateSource(), os.path.join("go", "src",
# and remove the "go" and "src" arguments.
# TODO(https://github.com/domokit/mojo/issues/768).
$EDITOR $MOJO_DIR/src/mojo/public/tools/bindings/generators/mojom_go_generator.py
```

## Building ##

```shell
cd $GOPATH/src/fuchsia.googlesource.com/thinfs
./scripts/build_mojom_bindings.sh
./scripts/build_apps.sh
```

## Testing ##

### Make a test filesystem ###
```shell
cd $MOJO_DIR/
dd if=/dev/zero of=test.fs bs=4096 count=16384
mkfs.fat $MOJO_DIR/test.fs
```

### Run the file system client application ###
```shell
cd $MOJO_DIR/src
mojo/devtools/common/mojo_run --enable-multiprocess --release \
        --args-for="mojo:blockd -logtostderr -path $MOJO_DIR/test.fs -readonly=true" \
        --args-for="mojo:fs -logtostderr" --args-for="mojo:fsclient -logtostderr" mojo:fsclient
```

If you see output that looks like
```
# I0603 16:43:45.294634    4996 main.go:78] Entry name=., type=Directory
# I0603 16:43:45.294969    4996 main.go:78] Entry name=.., type=Directory
```
then you know it worked.
