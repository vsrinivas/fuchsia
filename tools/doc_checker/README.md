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

Run the `fx host-tool` to locate `doc-checker`, build it if necessary and
execute it:

```
fx host-tool doc-checker
```

If the command above fails, the following are common error messages and
suggested solutions:

- "Can't load Kernel binary: Invalid kernel binary format version": the
  doc\_checker tool needs to be rebuilt with the command:

  ```
  fx build doc_checker
  ```

- "ERROR: found no artifacts in the GN graph": add `doc-checker` to your
  `fx set` line before executing `fx host-tool`:

  ```
  fx set <product>.<board> --with //tools/doc_checker
  ```

## Where is this used?

This program is used as part of the `doc-checker` infra commit queue builder.
