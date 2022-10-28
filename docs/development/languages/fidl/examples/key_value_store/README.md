# FIDL example: Key-value store

In this example, we start by creating a simple write-only key-value store, then
proceed to augment its functionality with various capabilities, such as reading
from the store, iterating over its members, and creating backups.

## Getting started {#baseline}

<<_baseline_tutorial.md>>

## Improving the design {#variants}

Each of the following sections explores one potential way that we could iterate
on the original design. Rather than building on one another sequentially, each
presents an independent way in which the base case presented above may be
modified or improved.

<!-- DO_NOT_REMOVE_COMMENT (Why? See: /tools/fidl/scripts/canonical_example/README.md) -->

### Adding support for reading from the store {#add_read_item}

<<_add_read_item_tutorial.md>>

### Adding support for iterating the store {#add_iterator}

<<_add_iterator_tutorial.md>>

<!-- /DO_NOT_REMOVE_COMMENT (Why? See: /tools/fidl/scripts/canonical_example/README.md) -->
