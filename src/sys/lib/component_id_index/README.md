# `component_id_index` library

This library parses and validate component ID index files.

This library has two clients:
* A build tool which validates and merges component ID index files written in
  JSON5 (see //tools/component_id_index), into a JSON-subset index file, and a
  FIDL-encoded index file.
* component_manager which consumes a single FIDL-encoded component ID index
  file.
* appmgr which consumes a single JSON-encoded component ID index file.

In order to keep the size of component_manager small, this library does not
directly depend on serde_json and serde_json5. Instead, this library accepts a
decoder implementation which helps decode a JSON/JSON5 string into an Index data
structure.
