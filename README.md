# tools

This repo contains tools used in Fuchsia build and development.

Go packages from here are automatically built and uploaded to CIPD and Google
Storage by bots using the [tools](https://fuchsia.googlesource.com/infra/recipes/+/master/recipes/tools.py) recipe.
To add a tool to the build:
 * Edit the [bot config](https://fuchsia.googlesource.com/infra/config/+/master/services/cr-buildbucket.cfg).
  * Find the `builder_mixins` section with `name: "tools"`.
  * Edit the JSON in `properties_j` to add a string to the `packages` list:
 ```json
 "fuchsia.googlesource.com/tools/cmd/your-new-tool"
 ```
