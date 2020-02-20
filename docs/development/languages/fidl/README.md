# FIDL

FIDL (**F**uchsia **I**nterface **D**efinition **L**anguage) is the IPC system for Fuchsia.

## Start here

The [tutorial](tutorial/README.md) presents a simple "*Hello, world*" client
and server, showing the FIDL language definitions and continuing with sections
specific to each supported target language (e.g., C++, Dart).

Read the [Introduction](intro/README.md) section to get a brief overview of what FIDL is,
including some of its design goals, requirements, and workflow.

## Language support

The FIDL code generator creates code in a multitude of target languages.
The following table gives you a reference to the details of the language implementaion,
as well as pointers to the code generated from the [tutorial's](tutorial/README.md)
"*Hello, world*" client and server examples.

Language                     | Examples
-----------------------------|---------------------------------------------
[C][c-lang]                  |                        [server][csrv-ex]
[Low-Level C++][llcpp-lang]  | [client][llcppcli-ex], [server][llcppsrv-ex]
[High-Level C++][hlcpp-lang] | [client][hlcppcli-ex], [server][hlcppsrv-ex]
[Dart][dart-lang]            | [client][dartcli-ex],  [server][dartsrv-ex]
[Rust][rust-lang]            | [client][rustcli-ex],  [server][rustsrv-ex]

# Contributing
Please read the [CONTRIBUTING](CONTRIBUTING.md) chapter for more information.

# References

* [ABI and Source Compatibility Guide](reference/abi-compat.md) &mdash; how to evolve FIDL APIs
* [API Rubric][fidl-api] &mdash; design patterns and best practices
* [Style Rubric][fidl-style] &mdash; style guide
* [Attributes](reference/attributes.md) &mdash; describes the available FIDL attributes
* [Bindings](reference/bindings.md) &mdash; requirements for FIDL language bindings
* [Compiler](reference/compiler.md) &mdash; describes the organization of the compiler
* [Linter](reference/linter.md) &mdash; describes how to check API readability with the FIDL linter
* [Editors](reference/editors.md) &mdash; discusses support for FIDL in IDEs and stand-alone editors
* [FIDL Tuning Proposals](reference/ftp/README.md) &mdash; accepted and rejected changes for FIDL
* [Grammar](reference/grammar.md) &mdash; the FIDL grammar
* [Host](reference/host.md) &mdash; summary of the parts of FIDL that are allowed on host
* [JSON IR](reference/json-ir.md) &mdash; a tour of the JSON Intermediate Representation
  (**JSON IR**) generator
* [Language](reference/language.md) &mdash; defines the syntax of the FIDL language
* [`library zx`](reference/library-zx.md) &mdash; the Zircon system library
* [Wire Format](reference/wire-format/README.md) &mdash; details the byte-by-byte organization
  of data

<!-- xrefs -->
[fidl-style]: /docs/development/languages/fidl/style.md
[fidl-api]: /docs/concepts/api/fidl.md

<!-- these in particular make the table manageable, and have the form:
     <language>-lang (the language part)
     <language>cli-ex (the client example)
     <language>srv-ex (the server example)
-->

[c-lang]: tutorial/tutorial-c.md
[csrv-ex]: /garnet/examples/fidl/echo_server_c/

[llcpp-lang]: tutorial/tutorial-llcpp.md
[llcppcli-ex]: /garnet/examples/fidl/echo_client_llcpp/
[llcppsrv-ex]: /garnet/examples/fidl/echo_server_llcpp/

[hlcpp-lang]: tutorial/tutorial-cpp.md
[hlcppcli-ex]: /garnet/examples/fidl/echo_client_cpp/
[hlcppsrv-ex]: /garnet/examples/fidl/echo_server_cpp/

[dart-lang]: /docs/development/languages/fidl/tutorial/tutorial-dart.md
[dartcli-ex]: https://fuchsia.googlesource.com/topaz/+/master/examples/fidl/echo_client_async_dart/
[dartsrv-ex]: https://fuchsia.googlesource.com/topaz/+/master/examples/fidl/echo_server_async_dart/

[rust-lang]: /docs/development/languages/fidl/tutorial/tutorial-rust.md
[rustcli-ex]: /garnet/examples/fidl/echo_client_rust/
[rustsrv-ex]: /garnet/examples/fidl/echo_server_rust/
