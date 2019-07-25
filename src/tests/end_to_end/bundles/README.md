# Bundles

The `end_to_end_deps` bundle lists runtime dependencies that are needed to run
end to end tests but are not compilation-time dependencies.

Note that these dependencies are added as `cached` packages, so they might not
update by just building a new version of Fuchsia and serving that build, instead
they need to be pushed or the image needs to be re-paved with the new version.
