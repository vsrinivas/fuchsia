# A11y Demo

## Running the demo
* Follow the instructions for [`tiles-session`](/src/session/examples/tiles-session/README.md) in order to use tiles-session.
* Include `--with=//src/ui/a11y/bin/demo:a11y-demo --with=//garnet/packages/prod:setui_client` in your `fx set`.
* Compile and pave the image
* Run :
  $ ffx config set setui true // only need to run once
  $ ffx setui accessibility set --screen_reader true
  $ ffx session add fuchsia-pkg://fuchsia.com/a11y-demo#meta/a11y-demo.cm
