# FIDL: C Language Bindings

This document is a description of the Fuchsia Interface Definition Language
(FIDL) implementation for C, including its libraries and code generator.

See [FIDL: Overview](index.md) for more information about FIDL's overall
purpose, goals, and requirements, as well as links to related documents.

[TOC]

## Design

Goals

*   Support encoding and decoding FIDL objects with C11.
*   Generate headers which are compatible with C11 and C++14.
*   Small, fast, efficient, 64-bit only.
*   Depend only on a small subset of the standard library.
*   Minimize code expansion through table-driven encoding and decoding.
*   Support only one usage style: native.

    Native Usage Style

*   Optimized to meet the needs of low-level systems programming.

*   Represent data structures whose memory layout coincides with the wire
    format.

*   Support in-place access and construction of FIDL objects.

*   Defer all memory allocation decisions to the client.

*   Code generator only produces type declarations, data tables, and simple
    inline functions.

*   Client is fully responsible for dispatching incoming method calls on
    interfaces (write their own switch statement and invoke argument decode
    functions).

    Encoding Tables

To avoid generating any non-inline code whatsoever, the C language bindings
instead produce encoding tables which describe how objects are encoded.

Introspection Tables

To allow for objects to be introspected (eg. printed), the C language bindings
produce introspection tables which describe the name and type signature of each
method of each interface and data structure.

Although small, introspection tables will be stripped out by the linker if
unused.

## Code Generator

Mapping Declarations

### Mapping FIDL Types to C Types

This is the mapping from FIDL types to C types which the code generator
produces.

| FIDL                                     | C Type                     |
|------------------------------------------|----------------------------|
| `bool`                                   | `bool`                     |
| `int8`                                   | `int8_t`                   |
| `uint8`                                  | `uint8_t`                  |
| `int16`                                  | `int16_t`                  |
| `uint16`                                 | `uint16_t`                 |
| `int32`                                  | `int32_t`                  |
| `uint32`                                 | `uint32_t`                 |
| `int64`                                  | `int64_t`                  |
| `uint64`                                 | `uint64_t`                 |
| `float32`                                | `float`                    |
| `float64`                                | `double`                   |
| `handle`, `handle?`, `handle`, `handle?` | `zx_handle_t`              |
| `string`, `string?`                      | `fidl_string_t`            |
| `vector`, `vector?`                      | `fidl_vector_t`            |
| `array<T>:N`                             | `T[N]`                     |
| `Interface`, `Interface?`                | typedef to `zx_handle_t`   |
| `request<I>`, `request<I>?`              | typedef to `zx_handle_t`   |
| `Struct`                                 | `struct Struct`            |
| `Struct?`                                | `struct Struct*`           |
| `Union`                                  | `struct Union`             |
| `Union?`                                 | `struct Union*`            |
| `Enum`                                   | typedef to underlying type |

### Mapping FIDL Identifiers to C Identifiers

TODO: discuss reserved words, name mangling

### Mapping FIDL Type Declarations to C Types

TODO: discuss generated macros, enums, typedefs, encoding tables

## Bindings Library

Dependencies

Only depends on Zircon system headers and a portion of the C standard library.

Code Style

To be discussed.

The bindings library uses C standard library style, eg. function names are
lower-case with underscores.

Data Types

# fidl_message_header

```
typedef struct fidl_message_header {
    uint64_t transaction_id;
    uint32_t flags;
    uint32_t ordinal;
} fidl_message_header_t;
```


Represents the initial part of every request or response message sent over a
channel. The header is immediately followed by the body of the payload.
Currently, there are no flags to be set, and so `flags` must be zero.


# fidl_string

```
typedef struct fidl_string {
    size_t size;   // number of UTF-8 code units (bytes), must be 0 if |data| is null
    uint8_t* data; // pointer to UTF-8 code units (bytes), or null
} fidl_string_t;
```

Holds a reference to a variable-length string.

When decoded, **data** points to the location within the buffer where the string
content lives, or **NULL** if the reference is null.

When encoded, **data** is replaced by **UINTPTR_MAX** when the reference is
non-null or **NULL** when the reference is null. The location of the string's
content is determined by the depth-first traversal order of the message during
decoding.

# fidl_vector

```
typedef struct fidl_vector {
    size_t size;  // number of elements, must be 0 if |data| is null
    void* data;   // pointer to element data, or null
} fidl_vector_t;
```

Holds a reference to a variable-length vector of elements.

When decoded, **data** points to the location within the buffer where the
elements live, or **NULL** if the reference is null.

When encoded, **data** is replaced by **UINTPTR_MAX** when the reference is
non-null or **NULL** when the reference is null. The location of the vector's
content is determined by the depth-first traversal order of the message during
decoding.

# fidl_encode()

Declared in
[system/ulib/fidl/include/lib/fidl/coding.h](
https://fuchsia.googlesource.com/zircon/+/HEAD/system/ulib/fidl/include/lib/fidl/coding.h),
defined in
[system/ulib/fidl/encoding.cpp](
https://fuchsia.googlesource.com/zircon/+/HEAD/system/ulib/fidl/encoding.cpp).


```
zx_status_t fidl_encode(
    const fidl_type_t* type,
    void* bytes,
    uint32_t num_bytes,
    zx_handle_t* handles,
    uint32_t max_handles,
    uint32_t* actual_handles_out,
    const char** error_msg_out);
```

Encodes and validates exactly **num_bytes** of the object in **bytes** in-place
by performing a depth-first traversal of the encoding data from **type**
to fix up internal references. Replaces internal pointers references with
`FIDL_ALLOC_ABSENT` or `FIDL_ALLOC_PRESENT` to indicate presence.
Extracts non-zero internal handle references out of **bytes**, stores up to
**max_handles** of them sequentially in **handles**, and replaces their location
in **bytes** with `FIDL_HANDLE_PRESENT` to indicate their presence. Sets
**actual_handles_out** to the number of handles stored in **handles**.

To prevent handle leakage, this operation ensures that either all handles within
**bytes** are moved into **handles** in case of success or they are all closed in
case of an error.

If a recoverable error occurs, such as encountering a null pointer for a
required sub-object, **bytes** remains in an unusable partially modified state.

All handles in **bytes** which were already been consumed up to the point of the
error are closed and **actual_handles_out** is set to zero. Depth-first traversal of
the object then continues to completion, closing all remaining handles in **bytes**.

If an unrecoverable error occurs, such as exceeding the bound of the buffer,
exceeding the maximum nested complex object recursion depth, encountering
invalid encoding table data, or a dangling pointer, the behavior is undefined.

On success, **bytes** and **handles** describe an encoded object ready to be sent
using `zx_channel_send()`.

If anything other than `ZX_OK` is returned, **error_msg_out** will be set.

Result is…

*   `ZX_OK`: success
*   `ZX_ERR_INVALID_ARGS`:
    *   **type** is null
    *   **bytes** is null
    *   **actual_handles_out** is null
    *   **handles** is null and **max_handles** != 0
    *   **type** is not a FIDL struct
    *   there are more than **max_handles** in **bytes**
    *   the total length of the object in **bytes** determined by the traversal
        does not equal precisely **num_bytes**
    *   **bytes** contains an invalid union field, according to **type**
    *   a required pointer reference in **bytes** was null
    *   a required handle reference in **bytes** was `ZX_HANDLE_INVALID`
    *   a bounded string or vector in **bytes** is too large, according to
        **type**
    *   a pointer reference in **bytes** does not have the expected value
        according to the traversal
    *   `FIDL_RECURSION_DEPTH` was exceeded (see
        [wire format](wire-format/index.md))


This function is effectively a simple interpreter of the contents of the
type. Unless the object encoding includes internal references which
must be fixed up, the only work amounts to checking the object size and the
ranges of data types such as enums and union tags.

# fidl_decode()

Declared in
[system/ulib/fidl/include/lib/fidl/coding.h](
https://fuchsia.googlesource.com/zircon/+/HEAD/system/ulib/fidl/include/lib/fidl/coding.h),
defined in
[system/ulib/fidl/decoding.cpp](
https://fuchsia.googlesource.com/zircon/+/HEAD/system/ulib/fidl/decoding.cpp).

```
zx_status_t fidl_decode(
    const fidl_type_t* type,
    void* bytes,
    uint32_t num_bytes,
    const zx_handle_t* handles,
    uint32_t num_handles,
    const char** error_msg_out);
```

Decodes and validates the object in **bytes** in-place by performing a
depth-first traversal of the encoding data from **type** to fix up internal
references. Patches internal pointers within **bytes** whose value is
`FIDL_ALLOC_PRESENT` to refer to the address of the out-of-line data they
reference later in the buffer. Populates internal handles within **bytes**
whose value is `FIDL_HANDLE_PRESENT` to their corresponding handle taken
sequentially from **handles**.

To prevent handle leakage, this operation ensures that either all handles in
**handles** from **handles[0]** to **handles[num_handles - 1]** are moved into **bytes** in case of
success or they are all closed in case of an error. After successfully decoding an object, you may
 use `fidl_object_close_handles()` to close any handles which you did not consume from it.

The **handles** array is not modified by the operation.

If a recoverable error occurs, a result is returned, **bytes** remains in an
unusable partially modified state, and all handles in **handles** are closed.

If an unrecoverable error occurs, such as encountering an invalid **type**,
the behavior is undefined.

If anything other than `ZX_OK` is returned, **error_msg_out** will be set.

Result is...

*   `ZX_OK`: success
*   `ZX_ERR_INVALID_ARGS`:
    *   **type** is null
    *   **bytes** is null
    *   **handles** is null but **num_handles** != 0.
    *   **handles** is null but **bytes** contained at least one valid handle
        reference
    *   **type** is not a FIDL struct
    *   the total length of the object determined by the traversal does not equal
        precisely **num_bytes**
    *   the total number of handles determined by the traversal does not equal
        precisely **num_handles**
    *   **bytes** contains an invalid union field, according to **type**
    *   a required pointer reference in **bytes** is `FIDL_ALLOC_ABSENT`.
    *   a required handle reference in **bytes** is `ZX_HANDLE_INVALID`.
    *   **bytes** contains an optional pointer reference which is marked
        as `FIDL_ALLOC_ABSENT` but has size > 0.
    *   a bounded string or vector in **bytes** is too large, according to
        **type**
    *   a pointer reference in **bytes** has a value other than
        `FIDL_ALLOC_ABSENT` or `FIDL_ALLOC_PRESENT`.
    *   a handle reference in **bytes** has a value other than
        `ZX_HANDLE_INVALID` or `FIDL_HANDLE_PRESENT`.
    *   `FIDL_RECURSION_DEPTH` was exceeded (see
        [wire format](wire-format/index.md))

This function is effectively a simple interpreter of the contents of the
type. Unless the object encoding includes internal references which
must be fixed up, the only work amounts to checking the object size and the
ranges of data types such as enums and union tags.

# fidl_object_close_handles()

```
zx_status_t fidl_object_close_handles(
const fidl_encoding_table_t* encoding_table,
const void* buf);
```

Releases all handles which appear in **buf**. Assumes that the object is valid.
The contents of **buf** are not modified by the operation.

If **buf** is null, does nothing and returns success.

Result is…

*   **NO_ERROR**: success
*   **ERR_INVALID_ARGS**:
    *   **encoding_table** was null

# fidl_object_count_handles()

```
zx_status_t fidl_object_count_handles(
const fidl_encoding_table_t* encoding_table,
const void* buf,
size_t* num_handles);
```

Counts how many non-zero handles appear in **buf**. Assumes that the object is
valid. The contents of **buf** are not modified by the operation.

If **buf** is null, sets **num_handles** to zero and returns success.

Result is...

*   **NO_ERROR**: success
*   **ERR_INVALID_ARGS**:

    *   **encoding_table** was null

    Introspection Functions

# fidl_object_print()

```
zx_status_t fidl_object_print(
const fidl_introspection_table_t* introspection_table,
const void* buf,
char* output,
size_t max_chars,
size_t* num_chars);
```

Print a human-readable description of the object in **buf** to a zero-terminated
string of up to **max_chars** characters (including the terminator) in
**output** for debugging. Returns the number of characters produced in
***num_chars**. Assumes that the object is valid. The contents of **buf** are
not modified by the operation.

If **buf** is null, sets **num_chars** to zero and returns success.

Result is...

*   **NO_ERROR**: success
*   **ERR_INVALID_ARGS**:
    *   **encoding_table** was null
    *   **output** was null but **buf** was non-null
*   **ERR_BUFFER_TOO_SMALL**:

    *   there was more than **max_chars** worth of content to print so the
        output was truncated, ***num_chars** will equal **max_chars**

    Encoding Table Declarations

Encoding tables are compact arrays of encoding entries which contain static
metadata about FIDL types. This information is compiled into FIDL-based programs
for the purposes of supporting efficient encoding and decoding of objects.

The encoding metadata contains:

*   the size of the object
*   the offsets of the fields within the object which require fixups (handles or
    object pointers; embedded structs and arrays that contain handles or object
    pointers)
*   pointers to encoding tables for out-of-line objects

One possible way to represent this information is as a sequence of entries which
are used to drive an interpreter.

[API TBD: this is a quick sketch for flavor only]

```
// encoding entry
typedef union fidl_encoding_entry {
    uint64_t value;              // packed opcode and arguments
    fidl_encoding_entry* table;  // reference to another table
} fidl_encoding_entry_t;

// encoding table
typedef fidl_encoding_entry_t* fidl_encoding_table_t;

// define a struct encoding table
##define FIDL_ENCODE_TABLE_STRUCT(obj_name, fields...) \
    const fidl_encoding_table_t type##_encoding = { \
        ??? stuff involving sizeof(obj_name) ???, \
        ##fields, \
        FIDL_ENCODE_END(), \
    };

##define FIDL_ENCODE_TABLE_UNION(obj_name, fields...)

// record offsetof(obj_name, field_name) and type of field
##define FIDL_ENCODE_FIELD_ARRAY(field_name, size, element_encoding_table)
##define FIDL_ENCODE_FIELD_HANDLE(field_name, required)
##define FIDL_ENCODE_FIELD_STRING(field_name, required)
##define FIDL_ENCODE_FIELD_VECTOR(field_name, element_encoding_table, required)
##define FIDL_ENCODE_FIELD_OBJECT(field_name, referent_encoding_table, required)

// mark end of encoding table
##define FIDL_ENCODE_END()
```

Introspection Table Declarations

Defined similarly to encoding tables but include names of methods and interfaces
as well as sufficient field type information to allow for formatting of their
contents.

TODO: It's possible we could generate both the encoding and introspection tables
from the same set of macros, although we want to ensure that the encoding tables
only contain the strict minimum information needed for validation.

Initialization Functions

# fidl_string_init()

```
void fidl_string_init(
void** buf,
fidl_string_t* ref,
size_t size,
const uint8_t* str);
```

Initializes the string at **ref**, optionally copying the contents of **str**
into **buf**.

Sets **ref->size** to **<code>size</code>**. Sets
**ref->data** to **buf**. Pads with zeros to the next
multiple of 8 bytes (for alignment, not termination). Advances
**buf** to point just beyond the padded region.

If **str** is non-null, copies **<code>size</code>** bytes from
**str** to **ref->data**.

# fidl_vector_init()

```
void fidl_vector_init(
void** buf,
fidl_vector_t* ref,
size_t num_elements,
    size_t element_size,
    const void* data);
```

Initializes the vector at **ref**, optionally copying the contents of **data**
into **buf**.

Sets **ref->size** to **num_elements**. Sets **ref->data** to ***buf**. Pads
with zeros to the next multiple of 8 bytes (for alignment, not termination).
Advances ***buf** to point just beyond the padded region.

If **data** is non-null, copies **num_elements * element_size** bytes from
**data** to **ref->data**.

Macros

```
##define FIDL_ALIGN(x) (((x) | 7) & ~7)
```

## Examples

Common Declarations used by Examples

The examples in this section use the following declarations.

# FIDL Declarations

```
library example;

interface Animal {
    Say(string text, handle<event> token);
    Add(int32 a, int32 b) -> (int32 sum);
};
```

# Generated Declarations

```
// Ordinal indices for methods in Animal interface.

enum {
example_Animal_Say_ordinal = 1;
example_Animal_Add_ordinal = 2;
};

// Args for example::Animal::Say().

typedef struct example_Animal_Say_args {
    fidl_string_t text;
    zx_handle_t token;
} example_Animal_Say_args_t;

// Args and result example::Animal::Add().

typedef struct example_Animal_Add_args {
    int32_t a;
    int32_t b;
} example_Animal_Add_args_t;

typedef struct example_Animal_Add_result {
int32_t sum;
} example_Animal_Say_result_t;
```

# Generated Encoding Tables

```
// Encoding tables for example::Animal::Say().
FIDL_ENCODE_TABLE_STRUCT(example_Animal_Say_args,
FIDL_ENCODE_FIELD_STRING(text, true),
FIDL_ENCODE_FIELD_HANDLE(token, true)
);

// Encoding tables for example::Animal::Add().
FIDL_ENCODE_TABLE_STRUCT(example_Animal_Add_args);
FIDL_ENCODE_TABLE_STRUCT(example_Animal_Add_result);
```

# Generated Introspection Tables (Optional)

```
// Introspection tables for example::Animal::Say().
FIDL_INTROSPECT_TABLE_STRUCT(example_Animal_Say_args,
    FIDL_INTROSPECT_FIELD_STRING(text, true),
    FIDL_INTROSPECT_FIELD_HANDLE(token, true)
);

// Introspection tables for example::Animal::Call().
FIDL_INTROSPECT_TABLE_STRUCT(example_Animal_Add_args,
    FIDL_INTROSPECT_FIELD_INT32(a),
    FIDL_INTROSPECT_FIELD_INT32(b)
);

FIDL_INTROSPECT_TABLE_STRUCT(example_Animal_Add_result,
    FIDL_INTROSPECT_FIELD_INT32(sum)
);

// Introspection tables for example::Animal interface.
FIDL_INTROSPECT_TABLE_INTERFACE(example_Animal,
"example", "Animal",
    FIDL_INTROSPECT_METHOD("Say", Say, example_Animal_Say_args_introspection),
    FIDL_INTROSPECT_METHOD("Add", Add, example_Animal_Add_args_introspection, example_Animal_Add_result_introspection)
);
```

Sending Messages

The client performs the following operations to send a message through a
channel.

*   Obtain a buffer large enough to hold the entire message.
*   Write the message header into the buffer (transaction id and method
    ordinal).
*   Write the message body into the buffer (method arguments).
*   Call **fidl_encode()** to encode the message and handles for
    transfer, taking care to pass a pointer to the **encoding table** of the
    message.
*   Call **zx_channel_send()** to send the message buffer and its associated
    handles.
*   Discard or reuse the buffer. (No need to release handles since they were
    transferred.)

For especially simple messages, it may be possible to skip the encoding step
altogether (or do it manually).

# Example Code

```
// Call Say("hello").
// This example could be tidied up a bit by expanding the FIDL API
// for building messages to take care of more of the grunt work
// or by generating helpers.
zx_status_t say_hello(
    zx_handle_t channel, const char* text, zx_handle_t token) {
  assert(strlen(text) <= MAX_TEXT_SIZE);

  uint8_t buf[sizeof(fidl_message_header_t) +
              sizeof(example_animal_Say_args_t) + FIDL_ALIGN(MAX_TEXT_SIZE)];
  memset(buf, 0, sizeof(buf));
  void* ptr = buf;

  fidl_message_header_t* header = (fidl_message_header_t*)ptr;
  ptr = header + 1;
  header->transaction_id = 1;
  header->flags = 0;
  header->ordinal = example_Animal_Say_ordinal;

  example_Animal_Say_args_t* args = (example_Animal_Say_args_t*)ptr;
  ptr = request + 1;
  fidl_string_init(&ptr, &args->text, strlen(text), text);
  args->token = token;

  size_t num_bytes = (uint8_t*)ptr - buf;
  zx_handle_t handles[1];
  size_t num_handles;
  const char* error_msg = nullptr;
  zx_status_t status = fidl_encode(example_Animal_Say_args_type,
      args, num_bytes - sizeof(fidl_message_header_t),
      handles, 1, &num_handles, &error_msg);
  if (status == ZX_OK) {
    status = zx_channel_write(
        channel, 0, msg, num_bytes, handles, num_handles);
  } else {
    FXL_LOG(WARNING) << "Failed to Say(\"hello\"): " << error_msg;
  }
  return status;
}
```

Receiving Messages

The client performs the following operations to receive a message through a
channel.

*   Obtain a buffer large enough to hold the largest possible message which can
    be received by this protocol. (May dynamically allocate the buffer after
    getting the incoming message size from the channel.)
*   Call **zx_channel_read()** to read the message into the buffer and its
    associated handles. (May instead use **zx_channel_read_async()** together
    with a **port**.)
*   Dispatch the message based on the method ordinal stored in the message
    header. If the message is invalid, close the handles and skip to the last
    step.
*   Call **fidl_decode()** to decode and validate the message and handles
    for access, taking care to pass a pointer to the **type** of the
    message.
*   If the message is invalid, skip to last step. (No need to release handles
    since they will be closed automatically by the decoder.)
*   Consume the message.
*   Call **fidl_object_close_handles()** to release any remaining handles stored
    within the message, taking care to pass a pointer to the **type**
    of the message body structure.
*   Discard or reuse the buffer.

For especially simple messages, it may be possible to skip the encoding step
altogether (or do it manually).

Dispatching Messages

The C language bindings do not provide any special affordances for dispatching
interface method calls. The client should dispatch manually based on the
interface method ordinal, such as by using a **switch** statement.

Introspection

The C language bindings provide a few functions to support table-based
introspection of objects, assuming the tables have not been stripped out.

Print an object as a human-readable string for debugging.

*   Call **fidl_object_print()** to print the object, taking care to pass a
    pointer to the **introspection table** of the interface and a buffer to hold
    the formatted output.

Traverse fields within a structure.

*   Iterate over each** **element in the **introspection table** of the
    structure.
