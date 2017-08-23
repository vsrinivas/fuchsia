# Data Organization

This document describes recommended practices for structuring data that is
stored in Ledger. We assume that the reader is already familiar with Ledger [API
surface](api_guide.md).

[TOC]

## Pages

Data stored in Ledger is grouped into separate independent key-value stores
called *pages*.

Deciding how to split data into pages has the following implications:

 - atomic changes across multiple values are possible only within one page, so
   if there's data that has to be modified together, it must belong to the
   same page
 - the current state of each page is either entirely present or entirely absent
   on disk (save for [lazy values](api_guide.md#lazy-values)), hence splitting
   data into multiple pages allows granular optimization of which data to keep
   on disk on each device

There are no restrictions on how many pages a client application can use.

## Entries

Each page stores an ordered list of key-value pairs, also called *entries*.

Both keys and values are arbitrary byte arrays - the client application is free
to construct the keys and values as needed based on their needs, but see some
guidance below.

### Key ordering

Entries are sorted by key in the lexicographic order of the byte array. Entry
retrieval methods always return them in order, it is also possible to use a
range query to retrieve only a part of the ordered list.

The lexicographic byte array ordering of the keys implies the following caveats:

 - **number ordering**: a key of `[1, 0, 0]` is ordered before the key of `[2]`.
   If ordering between the items is important, it is essential to add padding to
   any numbers that are part of the key, so that their width is fixed
 - **string ordering**: ordering of unicode strings in a way that matches human
   expectations is non-trivial and byte-array ordering of utf8/16/32 strings is
   unlikely to yield good results.

### Range queries

The entries stored in a page are sorted by keys, and can be retrieved using
either exact matching or [range queries](api_guide.md#range-queries).

Because of the range query support, it might be desirable to construct the keys
in a way that matches the querying needs of the application. For example, a
messaging application which needs to retrieve only the messages not older than a
week might wish to structure the data as follows:

 - **key**: {timestamp}-{message UUID}
 - **value**: {message content}

*** note
**Caveat**: as the range queries can currently only yield results in the
increasing order, in order to retrieve the newest messages, the timestamp needs
to be constructed as (BIG NUMBER minus the current time).
***

Then, a [range query](api_guide.md#range-queries) can be used to retrieve the
messages after the given timestamp.

*** note
Ledger does not support custom indexes and queries by value. Please do let us
know about your use cases if this limitation prevents you from adopting Ledger.
***

### Avoiding collisions

[UUIDs] should be used as part of the entry key in order to avoid unintended
collisions, where needed. For example, a messaging application might want to
include an UUID of a message stored in a Ledger entry as part of the entry key,
in order to avoid collisions between two messages of the same timestamp:

 - **key**: {timestamp}-**{message UUID}**
 - **value**: {message content}

## Granularity

Deciding how to split page data into entries has implications on querying
ability and [conflict resolution](api_guide.md#Conflict-resolution).

If two pieces of data can change **independently** (for example: a name and an
email of a contact in a contacts app), it is beneficial to put these pieces of
data in separate entries. Because conflicts are resolved entry-by-entry,
concurrent modifications of different entries can be automatically merged by the
default conflict resolution policy.

On the other hand, if two pieces of data are **related** (when one changes, the
other piece of data needs to change accordingly), we need to either:

 - put them in the same entry (and serialize them in the application)
 - put them in separate entries, but use a custom conflict resolution policy
   to preserve the invariants
 - put them in separate entries, but consistently always modify both of them
   together within a [transaction](api_guide.md#transactions)

[UUIDs]: https://en.wikipedia.org/wiki/Universally_unique_identifier
