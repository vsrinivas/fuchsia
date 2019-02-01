FIDL: Fuchsia Interface Description Language
============================================


FIDL (formerly known as mojom) is an IDL and encoding format used to describe
*interfaces* to be used on top of zircon message pipes. They are the standard
way applications talk to each other in Fuchsia.

FIDL includes libraries and tools for generating bindings from `.fidl` for
supported languages. Currently, the supported languages include C, C++, Dart,
Go, and Rust.


## IDE support

The [`language-fidl` plugin][language-fidl] provides syntax highlighting for
FIDL files in [Atom][atom].


[language-fidl]: https://atom.io/packages/language-fidl "FIDL Atom plugin"
[atom]: https://atom.io "Atom editor"
