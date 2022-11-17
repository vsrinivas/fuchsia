# The C++ zx library

The intention of this library is to provide an idiomatic C++ interface
to using Zircon handles and syscalls. This library provides type
safety and move semantics on top of the C calls.

Within this library, const is used to indicate that a method does not
modify the object's handle. Methods marked as const may still mutate
the underlying kernel object.

This library does not do more than that. In particular, thread and
process creation involve a lot more than simply creating the
underlying kernel structures. For thread creation you likely want to
use the libc (or libc++ etc.) calls, and for process creation the
launchpad APIs.

### zx::result<T?>

`zx::result<T?>` is a specialization for Zircon `zx_status_t` errors, based on
`fit::result<zx_status_t, T?>`, to make inter-op safer and more natural.

The namespace `zx` has aliases of the support types and functions in `fit`:
* `zx::ok`
* `zx::error`
* `zx::failed`
* `zx::success`
* `zx::as_error`

#### Returning and Using Values

Returning values with `zx::result` is the same as returning values with the
base `fit::result`. The status type supports the same value constructors,
conversions, and accessors as the base result type.

#### Returning Errors

`zx::result` enforces the convention that errors are distinct from values by
disallowing the value `ZX_OK` as an error value with a runtime assertion.

Instead of using `ZX_OK` to signal success, simply return a value or values.
When the value set is empty, return `zx::ok()` to signal success, just as with
the base result type.

##### zx::error_result Utility

`zx::error_result` is a simple alias of `zx::error<zx_status_t>` for
convenience.

#### Handling Errors

Handling errors with `zx::result` is the same as handling errors with
`fit::result`. All of the same constructs and accessors are available.

The `status_value()` accessor returns `ZX_OK` when the status is in the value
state. It is still invalid to call `error_value()` or `take_error()` in the
value state.

The `status_string()` accessor returns a string constant representing the value
returned by `status_value()`.

```C++
zx::result<> CheckValues(const foo&);
zx::result<foo> GetValues();

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

#### Propagating Errors and Values with zx::make_result

`zx::make_result` is a utility function to simplify forwarding `zx_status_t`
values through functions returning `zx::result<>` with an empty value set or
through functions returning `zx::result<T>`.

This is primarily to simplify interfacing with FFIs.

For functions that return zx_status_t with no output parameters:

```C++
zx_status_t check_foo_and_bar(const foo&, const &bar);

// Without using zx::make_result.
zx::result<> CheckValues(const foo& foo_in, const bar& bar_in) {
  const zx_status_t status = check_foo_and_bar(foo_in, bar_in);
  if (status == ZX_OK) {
    return zx::ok();
  } else {
    return zx::error_result(status);
  }
}

// With using zx::make_result.
zx::result<> CheckValues(const foo& foo_in, const bar& bar_in) {
  return zx::make_result(check_foo_and_bar(foo_in, bar_in));
}
```

For functions that return zx_status_t with one output parameter:

```C++
zx_status_t compute_foo(foo&);

// Without using zx::make_result.
zx::result<foo> Compute() {
  foo foo_out;
  const zx_status_t status = compute(foo_out);
  if (status == ZX_OK) {
    return zx::ok(foo_out);
  } else {
    return zx::error(status);
  }
}

// With using zx::make_result.
zx::result<foo> Compute() {
  foo foo_out;
  zx_status_t status = compute(foo_out);
  return zx::make_result(status, foo_out);
}

// And if you are very careful with argument order evaluation the above can be
// further simplified by eliminating the status variable.
zx::result<foo> Compute() {
  foo foo_out;
  return zx::make_result(compute(foo_out), foo_out);
}
```
