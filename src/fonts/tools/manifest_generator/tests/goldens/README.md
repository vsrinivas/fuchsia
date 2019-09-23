# Golden outputs

## `product_ab.font_manifest.json`

To regenerate, `cd` to the fuchsia source directory and execute:

```shell
fx build src/fonts/tools/manifest_generator && \
./out/default/host_x64/font_manifest_generator \
--fake-code-points --pretty-print --font-dir foo/bar \
--font-catalog src/fonts/tools/manifest_generator/tests/data/*.font_catalog.json \
--font-pkgs src/fonts/tools/manifest_generator/tests/data/*.font_pkgs.json \
--font-sets src/fonts/tools/manifest_generator/tests/data/product_ab.font_sets.json \
--output src/fonts/tools/manifest_generator/tests/goldens/product_ab.font_manifest.json
```
