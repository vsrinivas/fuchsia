Doc checker
===========

The present tool verifies the structure and content of documentation files.

## Features

This tool can generate a graph of the .md files in a given directory. It also
checks that HTTP links are valid and do not point to the same project (for which
relative links should be preferred). Lastly, it identifies documents which are
not connected to the rest of the documentation tree.

The menu structure encoding in the `_toc.yaml` files is validated to make
sure all files referenced exist as well as the general structure of the
yaml.

## Usage

First build the tool as part of the Fuchsia build. Specify the `doc-checker`
package during `fx set`.

```
fx set core.x64 --with //tools/doc_checker
fx build
```

Run `doc-checker` without argument.
```
cd ${FUCHSIA_SRC_DIR}
out/{default,x64}/dart-tools/doc-checker
```

See the tool's help for how to hold it right.

In order to view the generated graph, run (on Linux):
```
dot -Tpng graph.dot -o tree.png && feh tree.png
```

## Where is this used?

This program is used as part of the `doc-checker` infra commit queue builder.
