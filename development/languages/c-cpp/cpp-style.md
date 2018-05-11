C++ style guide
===============

The Fuchsia project follows the [Google C++ style guide][google-guide], with a
few exceptions.

By policy, code should be formatted by clang-format which should handle many
decisions.

### Exceptions

#### Braces

Always use braces `{ }` when the contents of a block are more than one line.
This is something you need to watch since Clang-format doesn't know to add
these.

```cpp
// Don't do this.
while (!done)
  doSomethingWithAReallyLongLine(
       wrapped_arg);

// Correct.
while (!done) {
  doSomethingWithAReallyLongLine(
       wrapped_arg);
}
```

#### Conditionals

Do not use spaces inside parentheses (the Google style guide discourages but
permits this).

Do not use the single-line form for short conditionals (the Google style guide
permits both forms):

```cpp
// Don't do this:
if (x == kFoo) return new Foo();

// Correct.
if (x == kFoo)
  return new Foo
```

[google-guide]: https://google.github.io/styleguide/cppguide.html
