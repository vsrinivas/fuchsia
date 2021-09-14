# bundle_fetcher tool

A tool that downloads and updates product manifests to contain the absolute URIs
and stores them in the out directory.

The tool is used in conjunction with //tools/artifactory.

### Development

The bundle_fetcher executable will be built at <build output>/host_x64/bundle_fetcher.
E.g. "$HOME/fuchsia/out/default/host_x64/bundle_fetcher"

### Unit tests

Unit tests can be run with:

```
$ fx test bundle_fetcher
```
