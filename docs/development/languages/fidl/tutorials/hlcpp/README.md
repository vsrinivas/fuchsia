# HLCPP tutorials

Note: if you are developing within the **fuchsia.git** tree, you are recommended
to use the [new C++ bindings][new-cpp] bindings, which offer better ergonomics,
thread-safety, and performance.

This section includes the following tutorials for using the HLCPP
FIDL bindings:

## Getting started

1. [Include FIDL in a C++ project][using-fidl]
2. [Write a server][server]
3. Write a client ([async][async] or [synchronous][sync])

## Topics

* [Request pipelining][pipelining]
* [Testing HLCPP protocols][testing]
* [FIDL type formatting with fostr][fostr]
* [Handling multiple clients][multi-client]
* [Unified services][services]

<!-- xrefs -->
[using-fidl]: basics/using-fidl.md
[server]: basics/server.md
[async]: basics/client.md
[sync]: basics/sync_client.md
[pipelining]: topics/request-pipelining.md
[testing]: topics/testing.md
[fostr]: topics/fostr.md
[multi-client]: topics/multiple-clients.md
[services]: topics/services.md
[new-cpp]: /docs/development/languages/fidl/tutorials/cpp/README.md
