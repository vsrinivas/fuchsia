# A11y Demo

## Running the demo
* Since settings are not completely implemented, edit topaz/runtime/flutter_runner/platform_view.cc
  and replace "SetSemanticsEnabled(screen_reader_enabled);" with
  "SetSemanticsEnabled(true);"
* Make sure fx set has --with-base="//src/ui/a11y/bin/demo:a11y_demo"
* Compile and pave the image
* fx shell
* Run :
  $ tiles_ctl start
  $ tiles_ctl add fuchsia-pkg://fuchsia.com/a11y_demo#meta/a11y_demo.cmx
