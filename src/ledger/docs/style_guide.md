# Style Guide

This document describes the conventions adopted in Ledger for the relevant
languages: English and C++.

[TOC]

## English

This section applies to written communication in docs, code comments, commit
messages, emails, etc.

### ledger vs Ledger vs the ledger vs ...

The project and the resulting application are called "Ledger". It is a proper
name, hence it is not preceded by an article.

**Do**: *Ledger synchronizes user data across devices*

**Don't**: *~~the Ledger~~ synchronizes user data across devices*

**Don't**: *~~ledger~~ synchronizes user data across devices*

All of the data stored in Ledger on behalf of a user is sometimes referred to as
this user's ledger. In that case "ledger" is a common name - not capitalized and
preceded by an article.

**Do**: *each user has a separate ledger*

**Don't**: *each user has ~~separate Ledger~~*

## C++

Ledger, like the rest of Fuchsia, follows [Google C++ Style Guide]. In a few
cases not covered there, we choose a convention.

### Naming of callback parameters

If there is a need to distinguish callback parameters of a method or a class,
call them `on_<event>`.

**Do**: `std::function<void(int)> on_done`

**Don't**: ~~`std::function<void(int)> done_callback`~~

### Usage of fxl::StringView vs std::string

This section discusses which string types to use for C++ API arguments.

Use a `fxl::StringView` (non-ref) if the functionality just reads the string:
`fxl::StringView` can handle any stream of bytes without copies.

Use a `std::string` (non-ref) if:

 - the functionality needs to take ownership of the string (it allows users of
   the functionality to `std::move` the string if they don't reuse it
   afterwards), or
 - you need to pass the string over to a function that requires an
   `std::string`.

You might want to use a `const std:string&` (const ref), if you need to call a
function that itself takes a `const std:string&` (and you cannot or don't want
to change it to take an `fxl::StringView`).

[Google C++ Style Guide]: https://google.github.io/styleguide/cppguide.html
