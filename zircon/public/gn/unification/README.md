# Build unification

The various templates defined in this folder are used to help with the migration
of elements of the zircon build into the greater Fuchsia build.

`zx_*` templates wrap existing target types and add a metadata generation step.
The resulting metadata is used in the Fuchsia build.

For more details, please visit http://fxbug.dev/3367.
