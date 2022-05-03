# A11y Demo

## Running the demo
* Make sure fx set has --with="//src/ui/a11y/bin/demo:a11y-demo",
  --with="//src/ui/tools/tiles_ctl:tiles_ctl", --with="//src/ui/tools/tiles:tiles", and
  --with="//garnet/packages/prod:setui_client"
* Compile and pave the image
* fx shell
* Run :
  $ ffx config set setui true // only need to run once
  $ ffx setui accessibility set --screen_reader true
  $ tiles_ctl start
  $ tiles_ctl add fuchsia-pkg://fuchsia.com/a11y-demo#meta/a11y-demo.cmx
