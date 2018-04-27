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

When unsure about which kind of string to use (i.e. when none of the foreign
APIs used in your functionality expect null-terminated strings or std::strings),
follow the following guidelines:

Use a `fxl::StringView` (const, non-ref) if the functionality just reads the
string: `fxl::StringView` can handle any stream of byte without copies.

Use a `std::string` (non-const, non-ref) if the functionality needs to take
ownership of the string: it allows users of the functionality to `std::move`
the string if they don't reuse it afterward.

[Google C++ Style Guide]: https://github.com/google/googletes://github.com/google/googletest
