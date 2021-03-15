# Product and Image Assembly

This area contains the libraries that are used for implementing the in-tree and out-of-tree tools for Product and Image
Assembly.

## Image Assembly

Image Assembly is focused on the end of the build process: when all the compiled "things" are put
together into the deliverable "images":

- `core` (or `base`) package (previously called `system_image`)
- `update` package for OTA
- Flashable artifacts like the fvm block image

## Product Assembly

Product Assembly is a larger process, where pieces of the platform are selected and combined with
pieces from the Product, to create the set of "things" to pass into Image Assembly.

## References

- [RFC-0072][rfc-0072] Standalone Image Assembly


[rfc-0072]: /docs/contribute/governance/rfcs/0072_standalone_image_assembly_tool.md
