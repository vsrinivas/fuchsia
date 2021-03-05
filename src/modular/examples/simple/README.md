# Simple Modular configuration

This is a minimal Modular configuration that runs Modular with the
`dev_session_shell` and `dev_story_shell` shells and no agents.
It can be used during development to run Modular with minimal dependencies,
or as a starting point for more complex configurations.

## Building

To add this configuration to your build, append
`--with-base "//src/modular/examples/simple"`
to the `fx set` invocation.

## Running

Once the configuration is included in the build, a product that includes
Modular will start the configuration on boot. For example:

```shell
$ fx set core.x64 \
    --with-base "//src/modular/bundles:framework" \
    --with-base "//src/modular/examples/simple"
```
