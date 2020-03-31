# Zircon Common Library (ZXC)

## Introduction

ZXC is a library of C++ vocabulary types, providing fundamental C++ primitives
that promote ergonomics, safety, and consistency. 

## Vocabulary Types

ZXC provides the following vocabulary types:

### zxc::result<E, Ts...>

`zxc::result` is an efficient implementation of the general value-or-error result
type pattern. The type supports returning either an error, or zero or one
values from a function or method.

This type is designed to address the following goals:
* Improve error handling and propagation ergonomics.
* Avoid the safety hazards and inefficiency of out parameters.
* Support effective software composition patterns.

#### Basic Usage

`zxc::result<E, T?>` may be used as the return type of a function or method.
The first template parameter `E` is the type to represent the error value. The
optional template parameter `T` is zero or one types to represent the value to
return on success. The value type may be empty, however, an error type is
required.

```C++
#include <lib/zxc/result.h>

// Define an error type and set of distinct error values. A success value is not
// necessary as the error and value spaces of zxc::result are separate.
enum class Error {
  InvalidArgs,
  BufferNotAvailable,
  RequestTooLarge,
};

// Returns a pointer the buffer and its size. Returns Error::BufferNotAvailable
// when the buffer is not available.
struct BufferResult {
  uint8_t* const buffer;
  const size_t buffer_size;
};
zxc::result<Error, BufferResult> GetBuffer();

zxc::result<Error> FillBuffer(uint8_t* data, size_t size) {
  if (data == nullptr || size == 0) {
    return zxc::as_error(Error::InvalidArgs);
  }
  
  auto result = GetBuffer()
  if (result.has_value()) {
    auto [buffer, buffer_size] = result.value();
    if (size > buffer_size) {
      return zxc::as_error(Error::RequestTooLarge);
    }
    std::memcpy(buffer, data, size);
    return zxc::ok();
  } else {
    return result.take_error();
  }
}
```

#### Returning Values

`zxc::result` emphasizes ease of use when returning values or otherwise
signaling success. The result type provides a number of constructors, most of
which are implicit, to make returning values simple and terse.

The following constructors are supported. Error and value constructors are
listed for completeness.

##### Copy/Move Construction and Assignment

The result type generally has the same trivial/non-trivial and copy/move
constructibility as the least common denominator of the error type `E` and the
value type `T`, if any. That is, it is only trivially constructible when all of
the supplied types are trivially constructible and it is only copy constructible
when all of the supplied types are copy constructible.

The result type is only copy/move assignable when the types `E` and `T` are
*trivially* copy/move assignable, otherwise the result is not assignable.

##### zxc::success

`zxc::result<E, T?>` is implicitly constructible from `zxc::success<U?>`, when
`T` is constructible from `U` or both are emtpy.

`zx::ok(U?)` is a utility function that deduces `U` from the given argument, if
any.

`zxc::success` is not permitted as the error type of `zxc::result`.

```C++
zxc::result<Error> CheckBounds(size_t size) {
  if (size > kSizeLimit) {
    return zxc::as_error(Error::TooBig);
  }
  return zxc::ok();
}
```

##### zxc::failed

The special sentinel type `zxc::failed` may be used as the error type when an
elaborated (enumerated) error is not necessary. When `zxc::failed` is used as
the error type, the result type is implicitly constructible from `zxc::failed`.

`zxc::failed` is not permitted as a value type of `zxc::result`.

```C++
zxc::result<zxc::failed> CheckBounds(size_t size) {
  if (size > kSizeLimit) {
    return zxc::failed();
  }
  return zxc::ok();
}
```

##### zxc::error<F>

The result type is implicitly constructible from any instance of `zxc::error<F>`,
where `E` is constructible from `F`. Using `zxc::error` is the only way to
return an error result. This prevents ambiguity between the value space and the
error space of the result, regardless of which types are used.

```C++
zxc::result<std::string, size_t> StringLength(const char* string) {
  if (string == nullptr) {
    // Uses the deduction guide to deduce zxc::error<const char*>. The zxc::result
    // error constructor is permitted because std::string is constructible from
    // const char*.
    return zxc::error("String may not be nullptr!");
  }
  return zxc::ok(strlen(string));
}
```

##### zxc::result<F, U?> with Compatible Error and Value

The result `zxc::result<E, T?>` type is implicitly constructible from any other
`zxc::result<F, U?>`, where the error type `E` is constructible from the error
type `F` and `T` is constructible from `U`, if any.

```C++
zxc::result<const char*, const char*> GetMessageString();

zxc::result<std::string, std::string> GetMessage() {
  return GetMessageString();
}
```

#### Discriminating Errors from Values

`zxc::result` has two predicate methods, `has_value()` and `has_error()`, that
determine whether a result represents success and contains zero or one values,
or represents an error and contains an error value, respectively.

```C++
zxc::result<const char*, size_t> GetSize();

void Example1() {
  auto result = GetSize();
  if (result.has_value()) {
    printf("size=%zu\n", result.value());
  }
  if (result.has_error()) {
    printf("error=%s\n", result.error_value());
  }
}

void Example2() {
  if (auto result = GetSize()) {
    printf("size=%zu\n", result.value());
  } else {
    printf("error=%s\n", result.error_value());
  }
}
```

#### Accessing Values

`zxc::result` supports several methods to access the value from a successful
result.

##### zxc:result::value() Accessor Methods

The value of a successful result may be accessed using the `value()` methods of
`zxc::result`.

```C++
zxc::result<Error, A> GetValues();

void Example() {
  auto result = GetValues();
  if (result.has_value()) {
    A a = result.value();
  }
}
```

##### zxc::result::take_value() Accessor Method

The value of a successful result may be propagated to another result using the
`take_value()` method of `zxc::result`.

```C++
zxc::result<Error, A> GetValues();

zxc::result<Error, A> Example() {
  auto result = GetValues();
  if (result.has_value()) {
    return result.take_value();
  } else {
    ConsumeError(result.take_error());
    return zxc::ok();
  }
}
```

#### Returning Errors

Returning errors with `zxc::result<E, T?>` always involves wrapping the error
value in an instance of `zxc::error<F>`, where `E` is constructible from `F`.
This ensures that error values are never ambiguous, even when `E` and `T` are
compatible types.

There are a variety ways to return errors:

##### Direct zxc::error

The most direct way to return an error is to use `zxc::error` directly.

`zxc::error` has a single argument deduction guide
`zxc::error(T) -> zxc::error<T>` when compiling for C++17 and above to simplify
simple error return values.

```C++
zxc::result<std::string, size_t> StringLength(const char* string) {
  if (string == nullptr) {
    return zxc::error<std::string>("String is nullptr!");
  }
  return zxc::ok(strlen(string));
}

zxc::result<std::string, size_t> StringLength(const char* string) {
  if (string == nullptr) {
    return zxc::error("String is nullptr!");
  }
  return zxc::ok(strlen(string));
}

// Error with multiple values.
using Error = std::pair<std::string, Backtrace>;

zxc::result<Error, size_t> StringLength(const char* string) {
  if (string == nullptr) {
    return zxc::error<Error>("String is nullptr!", Backtrace::Get());
  }
  return zxc::ok(strlen(nullptr));
}
```

##### zxc::as_error Utility Function

The single-argument utility function `zxc::as_error` may be used to simplify
returning a `zxc::error<F>` by deducting `F` from the argument type.

This function is a C++14 compatible alternative to the deduction guide.

```C++
zxc::result<std::string, size_t> StringLength(const char* string) {
  if (string == nullptr) {
    return zxc::as_error("String is nullptr!"); // Deduces zxc::error<const char*>.
  }
  return zxc::ok(strlen(string));
}
```

#### Handling Errors

`zxc::result` supports ergonomic error handling and propagation.

##### error_value() and take_error() Access Methods

The error value of a result may be accessed by reference using the
`error_value()` methods.

The error may be propagated using the `take_error()` method, which returns the
error value as an instance of `zxc::error<E>`, as required to pass the error to
`zxc::result`.

```C++
zxc::result<const char*> Example() {
  if (auto result = GetValues()) {
    // Use values ...
    return zxc::ok();
  } else {
    LOG_ERROR("Failed to get values: %s\n", result.error_value());
    return result.take_error();
  }
}
```

#### Relational Operators

`zxc::result` supports a variety of relational operator variants.

##### zxc::success and zxc::failure

`zxc::result` is always equal/not equal comparable to instances of
`zxc::success<>` and `zxc::failed`.

Comparing to `zxc::success<>` is equivalent to comparing to `has_value()`.
Comparing to `zxc::failed` is equivalent to comparing to `has_error()`.

```C++
zxc::result<Error, A> GetValues();

zxc::result<Error> Example() {
  auto result = GetValues();
  if (result == zx::ok()) {
    return zx::ok();
  }
  return result.take_error();
}
```

##### Any zxc::result with Compatible Value Types

`zxc::result<E, T?>` and `zxc::result<F, U?>` are comparable when `T` is
comparable to `U`, if any. The error types are not compared, only the
`has_value()` predicate and values are compared.

Comparing two result types has the same empty and lexicographic ordering as
comparing `std::optional<T>`.

```C++
zxc::result<Error, int> GetMin();
zxc::result<Error, int> GetMax();

bool TestEqual() {
  // Returns true when both results have values and the values are the same.
  return GetMin() == GetMax();
}
```

##### Any Type U Comparable to T

When `zxc::result<E, T>` has a single value type `T`, the result is comparable
to any type `U` that is comparable to `T`.

```C++
zxc::result<Error, std::string> GetMessage();

bool TestMessage() {
  // Returns true if there is a message and it matches the string literal.
  return GetMessage() == "Expected message";
}
```

### zxc::status<T?>

`zxc::status<T?>` is a specialization for Zircon `zx_status_t` error, based
on `zxc::result<zx_status_t, T?>`, to make inter-op safer and more natural.

#### Returning and Using Values

Returning values with `zxc::status` is the same as returning values with the
base `zxc::result`. The status type supports the same value constructors,
conversions, and accessors as the base result type.

#### Returning Errors

`zxc::status` enforces the convention that errors are distinct from values by
disallowing the value `ZX_OK` as an error value with a runtime assertion.

Instead of using `ZX_OK` to signal success, simply return a value or values.
When the value set is empty, return `zxc::ok()` to signal success, just as with
the base result type.

##### zxc::error_status Utility

`zx::error_status` is a simple alias of `zx::error<zx_status_t>` for
convenience.

#### Handling Errors

Handling errors with `zxc::status` is the same as handling errors with
`zxc::result`. All of the same constructs and accessors are available.

The `status_value()` accessor returns `ZX_OK` when the status is in the value
state. It is still invalid to call `error_value()` or `take_error()` in the
value state.

```C++
zxc::status<> CheckValues(const foo&);
zxc::status<foo> GetValues();

// Simple pass through.
zx_status_t check_foo_and_bar(const foo& foo_in) {
  // Returns a consistent value, regardless of error/value state.
  return CheckValues(foo_in).status_value();
}

// Normal error handling with control flow scoping.
zx_status_t get_foo_and_bar(foo* foo_out) {
  if (foo_out == nullptr) {
    return ZX_ERR_INVALID_ARGS;
  }

  auto result = GetValues();
  if (result.has_value()) {
    *foo_out = result.value();
    return ZX_OK; // Could return result.status_value() but this is more explicit.
  } else {
    return result.error_value();
  }
}
```

#### Propagating Errors with zxc::make_status(zx_status_t)

`zx::make_status` is a utility function to simplify forwarding `zx_status_t`
values through functions returning `zxc::status<>` with an empty value set.

This is primarily to simplify interfacing with FFIs.

```C++
zx_status_t check_foo_and_bar(const foo&, const &bar);

// Without using zx::forward_status.
zx::status<> CheckValues(const foo& foo_in, const bar& bar_in) {
  const zx_status_t status = check_foo_and_bar(foo_in, bar_in);
  if (status == ZX_OK) {
    return zx::ok();
  } else {
    return zx::error_status(status);
  }
}

// With using zx::forward_status.
zx::status<> CheckValues(const foo& foo_in, const bar& bar_in) {
  return zx::make_status(check_foo_and_bar(foo_in, bar_in));
}
```