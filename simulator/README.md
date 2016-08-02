# simulator

This is a pure-dart simulator built on top of handler and parser that allows to
visualize recipes and sessions graph of emulated sessions without any
dependencies on Mojo.

To run the recipe runner in order to produce session graph and
recipe diagrams:

1. install xdot and ensure it is in PATH. On Goobuntu one can run `apt-get
   install xdot`.

1. in `simulator/`:
  ```sh
  pub get
  ```

1. run:
  ```sh
  dart bin/run.dart \
    --module ../examples/recipes/reserve-restaurant.manifests.yaml \
    ../examples/recipes/reserve-restaurant.yaml
  ```

1. At the prompt, type "recipedot" to display a visualization of the
   recipe as dot graph. Close the xdot window in order to get the
   prompt back.

1. At the prompt, type "outputs" to see all available outputs in the
   session.

1. At the prompt, type "output-all" to run the session until all
   outputs have been emitted once.

1. At the prompt, type "sessiondot" to display a visualization of the
   session as dot graph. Close the xdot window to get the prompt back.

In order to add all known manifests to the recipe runner, run this:
```sh
dart bin/run.dart $(find ../examples/modules -type f \
  -name manifest.yaml -exec echo --module {} \;) \
  ../examples/recipes/reserve-restaurant.yaml
```
