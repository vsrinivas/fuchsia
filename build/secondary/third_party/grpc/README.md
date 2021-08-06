Adapted from [Chromium].

Steps to upgrade to a new version of GRPC, all relative to this directory:
1. Update `$FUCHSIA_DIR/integration/fuchsia/third_party/flower` to reference a new GRPC revision.
1. Install prerequisites:
   ```
   sudo apt install python3-mako`
   ```
1. Rebuild `BUILD.gn`:
   ```
   git -C $FUCHSIA_DIR/third_party/grpc submodule update --init
   cp template/BUILD.fuchsia.gn.template $FUCHSIA_DIR/third_party/grpc/templates
   (cd $FUCHSIA_DIR/third_party/grpc && tools/buildgen/generate_projects.sh)
   rm $FUCHSIA_DIR/third_party/grpc/templates/BUILD.fuchsia.gn.template
   mv $FUCHSIA_DIR/third_party/grpc/BUILD.fuchsia.gn BUILD.gn
   fx gn format --in-place BUILD.gn
   git -C $FUCHSIA_DIR/third_party/grpc submodule deinit --all
   ```

[Chromium]: https://source.chromium.org/chromium/chromium/src/+/main:third_party/grpc/README.chromium
