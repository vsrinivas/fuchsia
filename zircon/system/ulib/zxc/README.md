# Zircon Common Library (ZXC)

## Introduction

ZXC is a library of C++ vocabulary types, providing fundamental C++ primitives
that promote ergonomics, safety, and consistency.

## Vocabulary Types

ZXC provides the following vocabulary types:
* fit::result: An efficient value-or-error result type.
* zx::status: A specialization of `fit::result` for `zx_status_t` errors.

### fit::result<E, Ts...>

`fit::result` is an efficient implementation of the general value-or-error
result type pattern. The type supports returning either an error, or zero or one
values from a function or method.

This type is designed to address the following goals:
* Improve error handling and propagation ergonomics.
* Avoid the safety hazards and inefficiency of out parameters.
* Support effective software composition patterns.

#### Basic Usage

`fit::result<E, T?>` may be used as the return type of a function or method.
The first template parameter `E` is the type to represent the error value. The
optional template parameter `T` is zero or one types to represent the value to
return on success. The value type may be empty, however, an error type is
required.

```C++
#include <lib/fit/result.h>

// Define an error type and set of distinct error values. A success value is not
// necessary as the error and value spaces of fit::result are separate.
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
fit::result<Error, BufferResult> GetBuffer();

fit::result<Error> FillBuffer(uint8_t* data, size_t size) {
  if (data == nullptr || size == 0) {
    return fit::as_error(Error::InvalidArgs);
  }

  auto result = GetBuffer()
  if (result.is_ok()) {
    auto [buffer, buffer_size] = result.value();
    if (size > buffer_size) {
      return fit::as_error(Error::RequestTooLarge);
    }
    std::memcpy(buffer, data, size);
    return fit::ok();
  } else {
    return result.take_error();
  }
}
```

#### Returning Values

`fit::result` emphasizes ease of use when returning values or otherwise
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

##### fit::success

`fit::result<E, T?>` is implicitly constructible from `fit::success<U?>`, when
`T` is constructible from `U` or both are emtpy.

`fit::ok(U?)` is a utility function that deduces `U` from the given argument, if
any.

`fit::success` is not permitted as the error type of `fit::result`.

```C++
fit::result<Error> CheckBounds(size_t size) {
  if (size > kSizeLimit) {
    return fit::as_error(Error::TooBig);
  }
  return fit::ok();
}
```

##### fit::failed

The special sentinel type `fit::failed` may be used as the error type when an
elaborated (enumerated) error is not necessary. When `fit::failed` is used as
the error type, the result type is implicitly constructible from `fit::failed`.

`fit::failed` is not permitted as a value type of `fit::result`.

```C++
fit::result<fit::failed> CheckBounds(size_t size) {
  if (size > kSizeLimit) {
    return fit::failed();
  }
  return fit::ok();
}
```

##### fit::error<F>

The result type is implicitly constructible from any instance of
error space of the result, regardless of which types are used.

```C++
fit::result<std::string, size_t> StringLength(const char* string) {
  if (string == nullptr) {
    // Uses the deduction guide to deduce fit::error<const char*>. The
    // fit::result error constructor is permitted because std::string is
    // constructible from const char*.
    return fit::error("String may not be nullptr!");
  }
  return fit::ok(strlen(string));
}
```

##### fit::result<F, U?> with Compatible Error and Value

The result `fit::result<E, T?>` type is implicitly constructible from any other
`fit::result<F, U?>`, where the error type `E` is constructible from the error
type `F` and `T` is constructible from `U`, if any.

```C++
fit::result<const char*, const char*> GetMessageString();

fit::result<std::string, std::string> GetMessage() {
  return GetMessageString();
}
```

#### Discriminating Errors from Values

`fit::result` has two predicate methods, `is_ok()` and `is_error()`, that
determine whether a result represents success and contains zero or one values,
or represents an error and contains an error value, respectively.

```C++
fit::result<const char*, size_t> GetSize();

void Example() {
  auto result = GetSize();
  if (result.is_ok()) {
    printf("size=%zu\n", result.value());
  }
  if (result.is_error()) {
    printf("error=%s\n", result.error_value());
  }
}
```

#### Accessing Values

`fit::result` supports several methods to access the value from a successful
result.

##### fit:result::value() Accessor Methods

The value of a successful result may be accessed using the `value()` methods of
`fit::result`.

```C++
fit::result<Error, A> GetValues();

void Example() {
  auto result = GetValues();
  if (result.is_ok()) {
    A a = result.value();
  }
}
```

##### fit::result::operator*() Accessor Methods

`*my_result` is a syntax sugar for `my_result.value()`, when `my_result` is a
`fit::result`.

##### fit::result::take_value() Accessor Method

The value of a successful result may be propagated to another result using the
`take_value()` method of `fit::result`.

```C++
fit::result<Error, A> GetValues();

fit::result<Error, A> Example() {
  auto result = GetValues();
  if (result.is_ok()) {
    return result.take_value();
  } else {
    ConsumeError(result.take_error());
    return fit::ok();
  }
}
```

##### fit::result::operator->() Accessor Method

The members of the underlying value of a successful result may be accessed using
the `operator->()` overloads of `fit::result`.

```C++
struct FooBarResult {
  Foo foo;
  Bar bar;
};
fit::result<Error, FooBarResult> GetFooBar();

void Example() {
  auto result = GetFooBar();
  if (result.is_ok()) {
    ConsumeFoo(std::move(result->foo));
    ConsumeBar(std::move(result->bar));
  }
}
```

`fit::result` forwards to the underlying value's `operator->()` overload when
one is defined.

```C++
struct FooBarResult {
  Foo foo;
  Bar bar;
};
fit::result<Error, std::unique_ptr<FooBarResult>> GetFooBar();

void Example() {
  auto result = GetFooBar();
  if (result.is_ok()) {
    ConsumeFoo(std::move(result->foo));
    ConsumeBar(std::move(result->bar));
  }
}
```

#### Returning Errors

Returning errors with `fit::result<E, T?>` always involves wrapping the error
value in an instance of `fit::error<F>`, where `E` is constructible from `F`.
This ensures that error values are never ambiguous, even when `E` and `T` are
compatible types.

There are a variety ways to return errors:

##### Direct fit::error

The most direct way to return an error is to use `fit::error` directly.

`fit::error` has a single argument deduction guide
`fit::error(T) -> fit::error<T>` when compiling for C++17 and above to
simplify basic error return values.

```C++
fit::result<std::string, size_t> StringLength(const char* string) {
  if (string == nullptr) {
    return fit::error<std::string>("String is nullptr!");
  }
  return fit::ok(strlen(string));
}

fit::result<std::string, size_t> StringLength(const char* string) {
  if (string == nullptr) {
    return fit::error("String is nullptr!");
  }
  return fit::ok(strlen(string));
}

// Error with multiple values.
using Error = std::pair<std::string, Backtrace>;

fit::result<Error, size_t> StringLength(const char* string) {
  if (string == nullptr) {
    return fit::error<Error>("String is nullptr!", Backtrace::Get());
  }
  return fit::ok(strlen(nullptr));
}
```

##### fit::as_error Utility Function

The single-argument utility function `fit::as_error` may be used to simplify
returning a `fit::error<F>` by deducting `F` from the argument type.

This function is a C++14 compatible alternative to the deduction guide.

```C++
fit::result<std::string, size_t> StringLength(const char* string) {
  if (string == nullptr) {
    return fit::as_error("String is nullptr!"); // Deduces fit::error<const char*>.
  }
  return fit::ok(strlen(string));
}
```

#### Handling Errors

`fit::result` supports ergonomic error handling and propagation.

##### error_value() and take_error() Access Methods

The error value of a result may be accessed by reference using the
`error_value()` methods.

The error may be propagated using the `take_error()` method, which returns the
error value as an instance of `fit::error<E>`, as required to pass the error to
`fit::result`.

```C++
fit::result<const char*> Example() {
  if (auto result = GetValues()) {
    // Use values ...
    return fit::ok();
  } else {
    LOG_ERROR("Failed to get values: %s\n", result.error_value());
    return result.take_error();
  }
}
```

##### Augmenting Errors

`fit::result` supports agumented error types, where details about the error
are accumulated as the error propagates back through the call chain. The result
type conditionally overloads `operator+=` to append details to contained error.

The error type `E` must be a class: pointers, primitives, and enums are not
enabled. `E` must also overload `operator+=` to receive the value to append
to the error.

```C++
// Define an error type with augmentable details, similar to absl::Status.
class ErrorMsg {
 public:
  explicit ErrorMsg(Error error) : error_(error) {}
  ErrorMsg(Error error, std::string detail) : error_{error}, details_{{std::move(detail)}} {}

  Error error() const { return error_; }
  const auto& details() const { return details_; }

  std::string ToString() const;

  // fit::result detects this operator and enables augmentation of the error.
  ErrorMsg& operator+=(std::string detail) {
    details_.push_back(std::move(detail));
    return *this;
  }

 private:
  Error error_;
  std::vector<std::string> details_;
};

fit::result<Error> FillBuffer(uint8_t* data, size_t size);

fit::result<ErrorMsg> FillBufferFromVector(const std::vector<uint8_t>& vector) {
  // ErrorMsg is constructible from Error.
  fit::result<ErrorMsg> result = FillBuffer(vector.data(), vector.size());
  if (result.is_error()) {
    result += fit::error("Error while filling from vector.");
  }
  return result;
}
```

#### Relational Operators

`fit::result` supports a variety of relational operator variants.

##### fit::success and fit::failure

`fit::result` is always equal/not equal comparable to instances of
`fit::success<>` and `fit::failed`.

Comparing to `fit::success<>` is equivalent to comparing to `is_ok()`.
Comparing to `fit::failed` is equivalent to comparing to `is_error()`.

```C++
fit::result<Error, A> GetValues();

fit::result<Error> Example() {
  auto result = GetValues();
  if (result == fit::ok()) {
    return fit::ok();
  }
  return result.take_error();
}
```

##### Any fit::result with Compatible Value Types

`fit::result<E, T?>` and `fit::result<F, U?>` are comparable when `T` is
comparable to `U`, if any. The error types are not compared, only the
`is_ok()` predicate and values are compared.

Comparing two result types has the same empty and lexicographic ordering as
comparing `std::optional<T>`.

```C++
fit::result<Error, int> GetMin();
fit::result<Error, int> GetMax();

bool TestEqual() {
  // Returns true when both results have values and the values are the same.
  return GetMin() == GetMax();
}
```

##### Any Type U Comparable to T

When `fit::result<E, T>` has a single value type `T`, the result is comparable
to any type `U` that is comparable to `T`.

```C++
fit::result<Error, std::string> GetMessage();

bool TestMessage() {
  // Returns true if there is a message and it matches the string literal.
  return GetMessage() == "Expected message";
}
```

### zx::status<T?>

`zx::status<T?>` is a specialization for Zircon `zx_status_t` errors, based on
`fit::result<zx_status_t, T?>`, to make inter-op safer and more natural.

The namespace `zx` has aliases of the support types and functions in `fit`:
* `zx::ok`
* `zx::error`
* `zx::failed`
* `zx::success`
* `zx::as_error`

#### Returning and Using Values

Returning values with `zx::status` is the same as returning values with the
base `fit::result`. The status type supports the same value constructors,
conversions, and accessors as the base result type.

#### Returning Errors

`zx::status` enforces the convention that errors are distinct from values by
disallowing the value `ZX_OK` as an error value with a runtime assertion.

Instead of using `ZX_OK` to signal success, simply return a value or values.
When the value set is empty, return `zx::ok()` to signal success, just as with
the base result type.

##### zx::error_status Utility

`zx::error_status` is a simple alias of `zx::error<zx_status_t>` for
convenience.

#### Handling Errors

Handling errors with `zx::status` is the same as handling errors with
`fit::result`. All of the same constructs and accessors are available.

The `status_value()` accessor returns `ZX_OK` when the status is in the value
state. It is still invalid to call `error_value()` or `take_error()` in the
value state.

The `status_string()` accessor returns a string constant representing the value
returned by `status_value()`.

```C++
zx::status<> CheckValues(const foo&);
zx::status<foo> GetValues();

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
  if (result.is_ok()) {
    *foo_out = result.value();
    return ZX_OK; // Could return result.status_value() but this is more explicit.
  } else {
    LOG(ERROR, "Call to GetValues failed: %s\n", result.status_string());
    return result.error_value();
  }
}
```

#### Propagating Errors and Values with zx::make_status

`zx::make_status` is a utility function to simplify forwarding `zx_status_t`
values through functions returning `zx::status<>` with an empty value set or
through functions returning `zx::status<T>`.

This is primarily to simplify interfacing with FFIs.

For functions that return zx_status_t with no output parameters:

```C++
zx_status_t check_foo_and_bar(const foo&, const &bar);

// Without using zx::make_status.
zx::status<> CheckValues(const foo& foo_in, const bar& bar_in) {
  const zx_status_t status = check_foo_and_bar(foo_in, bar_in);
  if (status == ZX_OK) {
    return zx::ok();
  } else {
    return zx::error_status(status);
  }
}

// With using zx::make_status.
zx::status<> CheckValues(const foo& foo_in, const bar& bar_in) {
  return zx::make_status(check_foo_and_bar(foo_in, bar_in));
}
```

For functions that return zx_status_t with one output parameter:

```C++
zx_status_t compute_foo(foo&);

// Without using zx::make_status.
zx::status<foo> Compute() {
  foo foo_out;
  const zx_status_t status = compute(foo_out);
  if (status == ZX_OK) {
    return zx::ok(foo_out);
  } else {
    return zx::error(status);
  }
}

// With using zx::make_status.
zx::status<foo> Compute() {
  foo foo_out;
  zx_status_t status = compute(foo_out);
  return zx::make_status(status, foo_out);
}

// And if you are very careful with argument order evaluation the above can be
// further simplified by eliminating the status variable.
zx::status<foo> Compute() {
  foo foo_out;
  return zx::make_status(compute(foo_out), foo_out);
}
```

## Guidelines

Use the following guidelines to make the most of the result type's ergonomic and
safety features.

### Return Multiple Values Using Aggregates

Define an aggregate structure to return multiple values. Use meaningful names
for each aggregate member to improve readability.

```C++
struct CreateFooBarResult {
  Foo foo;
  Bar bar;
};
fit::<Error, CreateFooBarResult> CreateFooBar(Baz baz);
```

### Use Named Constructors for Complex Initialization

For types that require complex initialization that could fail, use a static
method (i.e. a named constructor) to perform the initialization. Make the
constructor private and only perform member initialization using values passed
in from the named constructor.

```C++
class Foo {
 public:
  fit::result<Error, Foo> Create(size_t size) {
    auto buffer_result = AllocateBuffer(size);
    if (buffer_result.is_error()) {
      return buffer_result.take_error();
    }
    auto bar_result = Bar::Create(size);
    if (bar_result.is_error()) {
      return bar_result.take_error());
    }
    return fit::ok(Foo{std::move(buffer_result.value()), size, std::move(bar_result.value())});
  }

 private:
  Foo(std::unique_ptr<uint8_t[]> buffer, size_t size, Bar bar)
    : buffer_{std::move(buffer)}, size_{size}, bar_{std::move(bar)} {}

  std::unique_ptr<uint8_t[]> buffer_;
  size_t size_;
  Bar bar_;
};
```

### Prefer Result Types to Output Parameters

Output parameters are often used when an operation might fail. Typically, the
return value is used to indicate success or (possibly enumerated) failure, while
other values are returned using the output parameters.

Output parameters introduce ambiguities that should be avoided:
* Are parameters pure outputs or mutable inputs?
* Is nullptr permitted or will it cause a CHECK-fail?
* What are the pre-conditions of the output states?
* What happens to the pre-existing states of the outputs on success?
* What states are the outputs left in on failure?
* What are the lifetime requirements of the output variables?

These ambiguities are often the source of subtle bugs. The result pattern avoids
ambiguity by construction: returning a value or an error is mutally exclusive.

Consider the following example using output parameters. It is difficult to infer
the answers to the questions above without referring to documentation. Even with
documentation, there is non-trivial cognitive load to check that the
implementation is correct, that callers follow the rules, and that the
documentation is consistent.

```C++
enum class Status {
  Ok,
  InvalidArgs,
  NoMemory,
  Clamped,
};

Status AllocateBuffer(size_t size, std::unique_ptr<uint8_t[]>* buffer_out, size_t* size_out) {
  if (size == 0) {
    buffer_out->reset(nullptr);
    size_out = 0; // Forgot to dereference size_out!
  }
  if (buffer_out == nullptr) {
    return Status::InvalidArgs;
  }

  // What about size_out == nullptr?

  if (size > kMaxSize) {
    size = kMaxSize;
  }

  fbl::AllocChecker checker;
  std::unique_ptr<uint8_t[]> buffer{new (&checker) uint8_t[size]};
  if (!checker.check()) {
    *size_out = 0; // What state should buffer_out be left in?
    return Status::NoMemory;
  }

  *buffer_out = std::move(buffer);
  *size_out = size;

  // Was size really clamped or just concidently the max?
  return size == kMaxSize ? Status::Clamped : Status::Ok;
}
```

Compare the previous example with the following example using the result type.
The `Status` type only enumerates the two possible reasons for failure, there is
no need to enumerate success states. The output states are well-defined and
there is very little cognitive load to validate the implementation and callers.

```C++
enum class Status {
  InvalidArgs,
  NoMemory,
};

struct AllocateResult {
  std::unique_ptr<uint8_t[]> buffer;
  size_t size;
};

fit::result<Status, AllocateResult> AllocateBuffer(size_t size) {
  if (size == 0) {
    return fit::error(Status::InvalidArgs);
  }
  if (size > kMaxSize) {
    size = kMaxSize;
  }

  fbl::AllocChecker checker;
  std::unique_ptr<uint8_t[]> buffer{new (&checker) uint8_t[size]};
  if (!checker.check()) {
    return fit::error(Status::NoMemory);
  }

  return fit::ok(AllocateResult{std::move(buffer), size});
}
```
