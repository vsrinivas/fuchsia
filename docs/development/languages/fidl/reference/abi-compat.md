# FIDL ABI and source compatibility guide

Date: 2019-03

Author: thatguy@google.com

Contributors: pascallouis@google.com, yifeit@google.com, cramertj@google.com

# Intended Audience

This doc is written for engineers who want to evolve FIDL APIs.
It describes what can be done safely without disrupting fellow teammates or
downstream clients.

## What is ABI compatibility?

ABI (application binary interface) compatibility is concerned with
the encoding and decoding of data over binary interfaces.
FIDL messages (method calls on FIDL protocols) end up serialized as bytes
over a zircon channel. Both channel endpoints (the client and server)
must agree on the size, ordering and meaning of the bytes.
A mismatch in expectations leads to binary incompatibility.

## Considerations when changing FIDL source

A change to a FIDL type that is source compatible (also known as API
compatible) means it is possible for someone to write code using the
generated code for a type that compiles both before and after the
change is made.
Making a source-incompatible change requires changing
all client source code at the same time (difficult if clients exist
outside the repository) to avoid breaking builds.

Note: Since Fuchsia sources (and related product code) exist in
multiple repositories with integration rollers and SDK releases
between them, it is not enough to ensure that the fuchsia.git
repository compiles.

> Disclaimer: The FIDL compatibility story today contains a number of
> edge cases.
> Language bindings may expose interfaces whose usage may or may not
> be resilient to changes in the underlying FIDL protocol.
> There are ongoing efforts to standardize these interfaces, but in the
> meantime, this document exists as a best-effort guide towards what
> types of code may be broken by what sorts of FIDL changes.
> If you discover an omission or mistake in this document, please
>  suggest the appropriate change, and refrain from enacting retribution on this
> document's authors.

# The Guide

[TOC]

## structs

> General guidance: once a struct is defined and in use, it cannot be changed.

#### Renaming the struct

```fidl
struct A {         struct A_new {
  int32 a;           int32 a;
  string b;          string b;
};                 };
```

![green checkmark](gc.png) **ABI Compatibility**: YES

**Transition Considerations**:

1. ~~Introduce an alias~~
2. Make a copy of the struct with the new name
3. Migrate all clients
4. ~~Remove alias~~
5. Delete the old struct

#### Reordering members

```fidl
struct A {         struct A {
  int32 a;           string b;
  string b;          int32 a;
};                 };
```

![red x](rx.png) **ABI Compatibility**: NO

**Transition Considerations**:

* **Positional initializers will break** e.g.,:
    * C++:
        * `auto a = A{10, "foo"};`
    * Go:
        * `A {10, "foo"};`
* **Prefer**:
    * C++:
        * `auto a = A{`<br>&nbsp;&nbsp;`.a = 10,`<br>&nbsp;&nbsp;`.b = "foo"`<br>`}`;
    * Go:
        * `a := A{`<br>&nbsp;&nbsp;`A:10.`<br>&nbsp;&nbsp;`B: "foo",`<br>`}`

#### Renaming members

```fidl
struct A {         struct A {
  int32 a;           int32 a_new;
  string b;          string b;
};                 };
```

![green checkmark](gc.png) **ABI Compatibility**: YES

![red x](rx.png) **Transition Considerations**: NO

* **Named reference / initialization will break:**
    * C++: `f(a.a_new)` and `auto a = A{.a = 10};`

#### Adding members

```fidl
struct A {         struct A {
  int32 a;           int32 a;
  string b;          string b;
  int32 c;
};                 };
```

![red x](rx.png) **ABI Compatibility**: NO

**Transition Considerations**:
* Depends on the language bindings.
* Go positional initializers &amp; Rust and Dart struct literals will break.

#### Removing members

```fidl
struct A {         struct A {
  int32 a;           int32 a;
  string b;
};                 };
```

![red x](rx.png) **ABI Compatibility**: NO

![green checkmark](gc.png) **Transition Consideration**:
* So long as `b` is not referenced any more (including in positional initializers).

## tables

#### Renaming the table

```fidl
table T {          table T_new {
  1: int32 a;        1: int32 a;
  2: string b;       2: string b;
};                 };
```

![green checkmark](gc.png) **ABI Compatibility**: YES

* **Transition Considerations**: **See: ["struct: Renaming the
  struct"](#renaming-the-struct)**

#### Reordering members

```fidl
table T {          table T {
  1: int32 a;        2: string b;
  2: string b;       1: int32 a;
};                 };
```

![green checkmark](gc.png) **ABI Compatibility**: YES
* Just don't change the ordinal values.

![green checkmark](gc.png) **Transition Considerations**: YES

#### Renaming members

```fidl
table T {          table T {
  1: int32 a;        1: int32 a_new;
  2: string b;       2: string b;
};                 };
```

![green checkmark](gc.png) **ABI Compatibility**: YES

![red x](rx.png) **Transition Considerations**: NO

#### Adding members

```fidl
table T {          table T {
  1: int32 a;        1: int32 a;
  2: string b;       2: string b;
                     3: int32 c;
};                 };
```

![green checkmark](gc.png) **ABI Compatibility**: YES

![green checkmark](gc.png) **Transition Considerations**: YES

#### Removing members

```fidl
table T {          table T {
  1: int32 a;        1: int32 a;
  2: string b;
};                 };
```

![green checkmark](gc.png) **ABI Compatibility**: YES

![green checkmark](gc.png) **Transition Considerations**: YES
* So long as `b` is not referenced any more.

#### Adding [NoHandles]

```fidl
                   [NoHandles]
table T {          table T {
  1: int32 a;        1: int32 a;
  2: string b;       2: string b;
};                 };
```

**ABI Compatibility**: **TODO**

**Transition Considerations**: **TODO**

## unions

Note: unions (vs **x**unions) are deprecated.
However, they follow similar rules to [structs](#structs).

## xunions

#### Reordering members

```fidl
xunion A {         xunion A {
  int32 a;           string b;
  string b;          int32 a;
};                 };
```

![green checkmark](gc.png) **ABI Compatibility**: YES

![green checkmark](gc.png) **Transition Considerations**: YES

#### Renaming members

```fidl
xunion A {         xunion A {
  int32 a;           int32 a_new;
  string b;          string b;
};                 };
```

![green checkmark](gc.png) **ABI Compatibility**: YES
* Use the `[Selector]` to retain compatibility:
  * `xunion A {`<br>&nbsp;&nbsp;`[Selector = "a"]`<br>&nbsp;&nbsp;`int32 a_new;`<br>&nbsp;&nbsp;`string b;`<br>`};`

![red x](rx.png) **Transition Considerations**: NO

#### Adding members

```fidl
xunion A {         xunion A {
  int32 a;           int32 a;
  string b;          string b;
                     int32 c;
};                 };
```

![green checkmark](gc.png) **ABI Compatibility**: YES

**Transition Considerations**:
* Depends on language bindings.
* Exhaustive matching (i.e., C++ `switch{}` on union tag) will break.

#### Removing members

```fidl
xunion A {         xunion A {
  int32 a;           int32 a;
  string b;
};                 };
```

![green checkmark](gc.png) **ABI Compatibility**: YES

![green checkmark](gc.png) **Transition Considerations**: YES
* So long as `b` is not referenced any more.

## vectors

#### Changing the size

```fidl
vector<T>:N        vector<T>:M
```

![green checkmark](gc.png) **ABI Compatibility**: YES

![green checkmark](gc.png) **Transition Considerations**:

* If the maximum size of the vector is **growing** (i.e. `M > N`) then all
  **consumers** _MUST_ be updated first.
* If the maximum size of the vector is **shrinking** (i.e. `M < N`) then all
  **producers** _MUST_ be updated first.

#### Changing the element type

```fidl
vector<T>:N        vector<U>:N
```

In many cases, this is neither ABI compatible, nor transitionable. Specific
cases can be discussed, but do not rely on this for evolvability of your
protocols.

![yellow warning](yw.png) **ABI Compatibility**: DEPENDS

![yellow warning](yw.png) **Transition Considerations**: DEPENDS

## strings

_Similar to vectors._

## enums

#### Reordering members

```fidl
enum E {           enum E {
  A = 1;             B = 2;
  B = 2;             A = 1;
};                 };
```

![green checkmark](gc.png) **ABI Compatibility**: YES

![green checkmark](gc.png) **Transition Considerations**: YES

#### Renaming members

```fidl
enum E {           enum E {
  A = 1;             A_NEW = 1;
  B = 2;             B = 2;
};                 };
```

![green checkmark](gc.png) **ABI Compatibility**: YES

![red x](rx.png) **Transition Considerations**: NO

#### Adding members

```fidl
enum E {           enum E {
  A = 1;             A = 1;
  B = 2;             B = 2;
                     C = 3;
};                 };
```

![green checkmark](gc.png) **ABI Compatibility**: YES

**Transition Considerations**:
* C++ `switch{}` without `default` will break
* Rust `match` without `"_"` will break

#### Removing members

```fidl
enum E {           enum E {
  A = 1;             A = 1;
  B = 2;
};                 };
```

![green checkmark](gc.png) **ABI Compatibility**: YES

**Transition Considerations**:
* Code which uses `E::B` will break

## protocol libraries & names

#### Renaming [Discoverable]

```fidl
[Discoverable]     [Discoverable]
protocol P {       protocol P_new {
  M1() -> ();        M1() -> ();
  M2() -> ();        M2() -> ();
};                 };
```

![red x](rx.png) **ABI Compatibility**: NO

* Renaming breaks service discoverability; names are used for service
  paths in namespaces/Directories.

![red x](rx.png) **Transition Considerations**: NO

#### Renaming non-[Discoverable]

```fidl
protocol P {       protocol P_new {
  M1() -> ();        M1() -> ();
  M2() -> ();        M2() -> ();
};                 };
```

![red x](rx.png) **ABI Compatibility**: NO

* Protocol names are part of method ordinal hashes.

![red x](rx.png) **Transition Considerations**: NO

#### Renaming the library

```fidl
library A:         library A.new:

protocol P {       protocol P {
  M1() -> ();        M1() -> ();
  M2() -> ();        M2() -> ();
};                 };
```

![red x](rx.png) **ABI Compatibility**: NO

* Library names are part of method ordinal hashes for all protocols within them.

![red x](rx.png) **Transition Considerations**: NO

## protocol methods

Note: These rules apply only to the methods, their names & ordering.
Protocol method arguments and return values follow the same
rules as [structs](#structs).

#### Reordering members

```fidl
protocol P {       protocol P {
  M1() -> ();        M2() -> ();
  M2() -> ();        M1() -> ();
};                 };
```

![green checkmark](gc.png) **ABI Compatibility**: YES

![green checkmark](gc.png) **Transition Considerations**: YES


#### Renaming members

```fidl
protocol P {       protocol P {
  M1() -> ();        M1_new() -> ();
  M2() -> ();        M2() -> ();
};                 };
```

![green checkmark](gc.png) **ABI Compatibility**: YES

* Use the `[Selector]` to retain compatibility:
  * `protocol P {`<br>&nbsp;&nbsp;`[Selector= "M1"]`<br>&nbsp;&nbsp;`M1_new() -> ();`<br>&nbsp;&nbsp;`M2() -> ();`<br>`};`

![red x](rx.png) **Transition Considerations**: NO

#### Adding members

```fidl
protocol P {       protocol P {
  M1() -> ();        M1() -> ();
  M2() -> ();        M2() -> ();
                     M3() -> ();
};                 };
```

![green checkmark](gc.png) **ABI Compatibility**: YES

![green checkmark](gc.png) **Transition Considerations**: YES

* Add `[Transitional]` to the new member:
  * `protocol P {`<br>&nbsp;&nbsp;`M1() -> ();`<br>&nbsp;&nbsp;`M2() -> ();`<br>&nbsp;&nbsp;`[Transitional="msg"]`<br>&nbsp;&nbsp;`M3() -> ();`<br>`}`
* See [FTP-021][ftp021]

#### Removing members

```fidl
protocol P {       protocol P {
  M1() -> ();        M1() -> ();
  M2() -> ();
};                 };
```

![green checkmark](gc.png) **ABI Compatibility**: YES

![green checkmark](gc.png) **Transition Considerations**: YES

1. Add `[Transitional]` to `M2()`
2. Remove references
3. Delete from `.fidl`

* See [FTP-021][ftp021]

#### protocol method arguments & return values

Follow the same rules as [structs](#structs).

<!-- xrefs -->
[ftp021]: /docs/development/languages/fidl/reference/ftp/ftp-021.md
