# test-driver

This component is used a test component for `ffx component copy`. Data/files will be copied from and to this component for testing purposes.

## Building

To add this component to your build, append
`--with src/developer/ffx/plugins/component/copy/test-driver`
to the `fx set` invocation.

## Running

Use `ffx component run` to launch this component into a restricted realm for development purposes:

```
$ ffx component run /core/ffx-laboratory:test-driver fuchsia-pkg://fuchsia.com/test-driver#meta/test-driver.cm
```

## Testing

Assuming that the instructions from above have been completed, the moniker of the component to be `/core/ffx-laboratory:test-driver`, which is what we will use for our `ffx component copy` example commands. 

Once your test component has been set up, here are some example tests can be run to verify that `ffx component copy` is properly working.


```
# set up environment variables
TEST_DRIVER=/core/ffx-laboratory:test-driver::
LOCAL_RESOURCE_DIR=src/developer/ffx/plugins/component/copy/test-driver/resources
TMP_DIR=/tmp

# standard copy from host to target device 
ffx component copy $LOCAL_RESOURCE_DIR/file.txt $TEST_DRIVER/tmp/file.txt

# copy from host to target device with new file name
ffx component copy $LOCAL_RESOURCE_DIR/file.txt $TEST_DRIVER/tmp/new.txt

# copy from host to target device inferring the destination file name
ffx component copy $LOCAL_RESOURCE_DIR/file.txt $TEST_DRIVER/tmp/

# standard copy from target to host device 
ffx component copy $TEST_DRIVER/pkg/resources/foo.txt $TMP_DIR/foo.txt

# copy from host to target device with new file name
ffx component copy $TEST_DRIVER/pkg/resources/foo.txt $TMP_DIR/bar.txt

# copy from host to target device inferring the destination file name
ffx component copy $TEST_DRIVER/pkg/resources/foo.txt $TMP_DIR/
```


```
$ fx test test-driver-tests
```

