Adapted from [Chromium].

Steps to upgrade to a new version of GRPC, all relative to this directory:
1. Update the template:
   ```
   curl -sfSL \
     https://chromium.googlesource.com/chromium/src/+/main/third_party/grpc/template/BUILD.chromium.gn.template?format=TEXT | \
     base64 --decode | sed -E "s/([\"'])src/\1./g" > template/BUILD.fuchsia.gn.template
   ```
1. Apply Fuchsia-specific patch:
   ```
   patch -d $FUCHSIA_DIR -p1 --merge < template/fuchsia.patch
   ```
1. Resolve conflicts.
1. Commit the result.
1. Regenerate Fuchsia-specific patch:
   1. Download the original template (see curl above).
   1. Generate the patch:
      ```
      git diff -R template/BUILD.fuchsia.gn.template > template/fuchsia.patch
      ```
   1. Restore the template:
      ```
      git checkout template/BUILD.fuchsia.gn.template
      ```
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
