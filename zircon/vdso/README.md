# FIDL library zx

FIDL library `zx` describes Fuchsia's syscall interface. The modeling of the
interface is currently experimental and undergoing a slow evolution. Described
in [RFC 190][rfc], [fxbug.dev/110021][110021] tracks the process of realizing
`zx` as non-experimental, _pure_ FIDL.

The current version of the library - dubbed "v2" - purposefully overfits to
modeling the C vDSO interface. [fxbug.dev/110294][110294] describes the
reasoning and methodology of this approach, which is ultimately an expedient
and temporary measure. As proposed in the RFC, the ultimate modeling will
expressly avoid this and attempt to frame things in a more general way with
[_"bring your own runtime"_][byor] in mind.

## Conventions & Quirks

### Naming

Library `zx` adheres to standard FIDL [style guide][fidl-naming]... except for
a handful of legacy 'snake case' uses (e.g., `zx/status`). Updating these
definitions to be adhering and camel case is tracked by
[fxbug.dev/109734][109734].

### Experimental types

Per the 'C overfit' v2 design, several C-like types are temporarily introduced
to library `zx` behind the experimental `zx_c_types` flag:

* `experimental_pointer<T>`: represents a pointer (to a type `T`) and has the
FIDL ABI of a `uint64`.

* `usize`: represents the size of a region of memory and has the FIDL ABI of
`uint64`. Its binding in Fuchsia-targeting C and C++ code is meant to be
`size_t`.

* `uintptr`: an integral type of sufficient size so as to be able to hold a
pointer, practically regarded as an address in a remote address space. It has
the FIDL ABI of `uint64` and its binding in Fuchsia-targeting C/C++ code is
intended to be `uintptr_t`.

* `uchar`: represents an opaque, unsigned, 8-bit 'character' (e.g., ASCII or a
UTF-8 code point) and has the FIDL ABI of `uint8`. Its binding in
Fuchsia-targeting C/C++ code is intended to be `char`.

### Representation of syscalls

Currently lacking a means of declaring syscalls in FIDL in a first-class
manner, we use the following convention for declaring a logical grouping of
syscalls:

```
@transport("Syscall")
protocol NounPhrase {
    VerbPhrase(struct{...}) -> (struct{...}) error status;

    ...
};
```

This method corresponds to `zx_${noun_phrase}_${verb_phrase}()`: its 'in'
parameters are given in order in the request struct and its 'out' parameters
given in order in the response struct. The `error status`  clause can be dropped
in the case of syscalls that do not return `zx_status_t`.

See [@no_protocol_prefix](#no-protocol-prefix) for a possible protocol
annotation.

See [@blocking](#blocking), [@const](#const), [@internal](#internal),
[@next](#next), [@noreturn](#noreturn), and [@vdsocall](#vdsocall),
[@testonly, @test_category1, and @test_category2](#testonly-test_category1-test_category2)
for possible method annotations.

### Representation of buffers

We represent the buffers of caller-owned memory passed into syscalls as
`vector`s, implicitly representing separate data and length parameters.

See [@embedded_alias](#embedded_aliasalias_name), [@size32](#size32), and
[@voidptr](#voidptr) for possible annotations.

Syscall buffer specification should be holistically designed in the context
of [fxbug.dev/110021][110021].

### Documentation

TODO(fxbug.dev/110021): Have `fidldoc` emit the syscall documentation
(//docs/reference/syscalls/) in a more first-class way. Currently, the content
of those markdown pages are expected to appear as the docstrings of the
associated FIDL syscall declarations.

TODO(fxbug.dev/110021): Settle on a convention for how official syscall
documentation should refer to its FIDL source-of-truth. For now, we ignore
the FIDL representation and refer solely to the associated C bindings.

### Special attributes

#### @blocking

Annotates a syscall declaration to indicate that the calling thread is blocked
until the call returns.

This should be formalized as something known to and validated by `fidlc` - or
redesigned altogether - in the context of [fxbug.dev/110021][110021].


#### @const

Annotates a @vdsocall-decorated syscall declaration to indicate that the
function is "const" in the sense of `__attribute__((__const__))`.

This information is not a part of public ABI - relevant only to implementation
details - and should be designed away in the context of
[fxbug.dev/110021][110021].


#### @embedded_alias("<alias_name>">)

Annotates a `vector` or `experimental_pointer` whose element/pointee type is an
alias. This attribute serves to expediently inject the name of the alias into
the related IR. Only a full resolution of an alias survives into the IR today,
a bug which is tracked by [fxbug.dev/105758][105758]. Once resolved, this
attribute should be straightforwardly removed.


#### @handle_unchecked

Annotates a handle as a syscall parameter to indicate that it is
released/consumed by that call. Similarly so for a vector of handles.

This should be formalized as something known to and validated by `fidlc` - or
redesigned altogether - in the context of [fxbug.dev/110021][110021].


#### @inout

Annotates a syscall parameter to indicate - with the C bindings in mind - that
it should be treated as both an 'in' and an 'out' parameter. If applied to a
vector, the implicit data parameter is regarded as an out parameter, while the
implicit size parameter as regarded as an 'in'.

This notion should be redesigned more holistically in the context of
[fxbug.dev/110021][110021].


#### @internal

Annotates a "syscall" declaration to indicate that the call is not a part of
public ABI and describes internal vDSO logic.

This information is not a part of public ABI - relevant only to implementation
details - and should be designed away in the context of
[fxbug.dev/110021][110021].


#### @next

Annotates an element to indicate that the feature is not yet 'well-baked' and
whose should not be unconditionally distributed in the SDK.

This should be formalized as something known to and validated by `fidlc` - or
redesigned altogether - in the context of [fxbug.dev/110021][110021].


#### @no_protocol_prefix

Annotates a protocol representing a family of syscalls to indicate that the
name of the protocol should not be factored in to the name of its constituent
syscalls. In this case, the protocol name is arbitrary and the syscalls members
include the would-be family namespacing directly into their names. This is
expedient in the cases where the would-be protocol name clashes with that of
another FIDL element in the library.

For example, `zx_clock_read()` would naturally be represented - in `zx` v2 - as
```
@transport("Syscall")
protocol Clock {
    ...

    Read(resource struct {
        handle handle;
    }) -> (struct {
        now time;
    }) error status;

    ...
};
```
However, `zx.Clock` already exists as an enum, preventing us from declaring a
protocol with that same name. So instead we spell this as
```
@no_protocol_prefix
@transport("Syscall")
protocol ClockFuncs {  // An arbitrary name.
    ...

    ClockRead(resource struct {
        handle handle;
    }) -> (struct {
        now time;
    }) error status;

    ...
};
```

This is a hack and the collisions in question should be whittled down over the
course of [fxbug.dev/110021][110021]. At that point, this attribute should go
away.


#### @noreturn

Annotates a syscall declaration to indicate that the call will not return.

This should be formalized as something known to and validated by `fidlc` - or
redesigned altogether - in the context of [fxbug.dev/110021][110021].


#### @out

Annotates a syscall parameter in the request struct to indicate - with the C
bindings in mind - that it actually should be treated as an 'out' parameter.

This notion should be redesigned more holistically in the context of
[fxbug.dev/110021][110021].

#### @release

Annotates a handle as a syscall parameter to indicate that it is
released/consumed by that call. Similarly so for a vector of handles.

This should be formalized as something known to and validated by `fidlc` - or
redesigned altogether - in the context of [fxbug.dev/110021][110021].


#### @size32

Annotates vector syscall parameters to indicate that the implicit size
parameter is 32-bit.

Syscall buffer specification should be holistically designed in the context
of [fxbug.dev/110021][110021].


#### @testonly, @test_category1, @test_category2

These are test-specific and it should be rethought in the context of
[fxbug.dev/110021][110021] whether these elements should be defined in `zx`
proper.


#### @vdsocall

Annotates a syscall declaration to indicate that the call does not actually
enter the kernel and is properly defined within the vDSO.

This information is not a part of public ABI - relevant only to implementation
details - and should be designed away in the context of
[fxbug.dev/110021][110021].


#### @voidptr

Annotates `experimental_pointer<byte>` or `vector<byte>` to indicate to C
backends that the mapped types should be represented with `void*`.

This should be redesigned altogether in the context of
[fxbug.dev/110021][110021].


[105758]: http://fxbug.dev/105758
[109734]: http://fxbug.dev/109734
[110021]: http://fxbug.dev/110021
[110294]: http://fxbug.dev/110294
[byor]: https://fuchsia.dev/fuchsia-src/concepts/principles/simple?hl=en
[fidl-naming]: https://fuchsia.dev/fuchsia-src/development/languages/fidl/guides/style#names
[rfc]: https://fuchsia.dev/fuchsia-src/development/languages/fidl/guides/style?#names