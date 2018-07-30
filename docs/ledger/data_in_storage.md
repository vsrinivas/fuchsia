# Data in Storage

Storage implements persistent representation of data held in Ledger. Such data
include:

- commit, value and tree node objects
- journal entries and metadata
- information on head commits
- information on which objects have been synced to the cloud
- other synchronization metadata, such as the time of the last synchronization
  to the cloud

All data and metadata added in storage are persisted using LevelDB. For each
[page](data_organization.md#Pages) a separate LevelDB instance is created in a
dedicated filesystem path of the form:
`{repo_dir}/content/{ledger_dir}/{page_id_base64}/leveldb`.

Additionally, metadata about all pages of a single user are persisted in a
separate LevelDB instance. This includes information such as the last time a
page was used.

The rest of the document describes the key and value representation for each row
created in LevelDB to store each data type.

[TOC]

# Per Page data storage

## Commit objects

- Row key: `commits/{commit_id}`
- Row value: Contains the creation `timestamp`, `generation` (i.e. length of the
  longest path to the first commit created for this page), `root_node_id` and
  `parent_ids`, as serialized by Flatbuffers (see [commit.fbs]).

## Value and tree node objects

In storage there is no difference in the representation of value and node
objects. Since these two types of objects can be quite big, they might be split
in multiple pieces. Each **piece** is serialized in LevelDB as follows:

- Row key: `objects/{object_digest}`
- Row value: `{object_content}`

For value objects, `{object_content}` is the actual user defined content, while
for tree nodes, it is the list of node entries (key, object_id and priority),
references to child nodes, and the node level. Tree node content is serialized
using Flatbuffers (see [tree_node.fbs]).

Note that when a Ledger user inserts a key-value pair, the key is stored in a
tree node, while the value is stored separately, as described above.

### Splitting objects in pieces

Value or tree node objects might be split into smaller pieces that are stored
separately. When processing a large object, its content is fed into a rolling
hash which determines how the object should be split into chunks. For each such
chunk, whose size is always between 4KiB and 64KiB, an identifier is computed
and added in a list. Based on the rolling hash algorithm this list is split into
index files, containing references (identifiers) towards either chunks of the
original object's content, or other index files. At the end of the algorithm,
the data chunks and index files form a tree, where the content of the object is
stored on the leaves.

See also [split.h] and [split.cc] for more details.

## Journals

Changes in Page (Put entry, Delete entry, Clear page) that have not yet been
committed are organized in [journals]. A journal can be explicit, when it is
part of an explicitly created transaction or part of a merge commit, or
implicit, for any other case. On a system crash all explicit journals are
considered invalid and once the system restarts they are removed from the
storage. Implicit ones on the other hand, are immediately committed on system
restart.

A common prefix for all explicit journal entries (`journals/E`) helps remove
them all together when necessary, and an additional metadata row, for implicit
journals only, helps retrieve the ids of the not-yet-committed journals.

Journal entry keys (for both implicit and explicit journals) are serialized in
LevelDB as:

Row key: `journals/{journal_id}/entry/{user_defined_key}`

`{journal_id}` has an `E` prefix if the journal is explicit or an `I` prefix if
it's implicit.

- If the journal entry is about adding a new or updating an existing Ledger
key-value pair, then:

  Row value: `A{priority_byte}{object_identifier}`

  Where `{priority_byte}` is either `E` if the priority is Eager, or `L` if it's
  Lazy.

- If the journal entry is about removing and existing key-value pair, the value
  is:

  Row value: `D`

Moreover, if a journal contains a page clear operation, a row with an empty
value is added to the journal. If it is present, when the journal is commited,
the previous state of the page must be discarded.

- Row key: `journals/{journal_id}/C`
- Row value: (empty value)

### Metadata row for implicit journals

For every implicit journal an additional row is kept in LevelDB:
- Row key: `journals/implicit_metadata/{journal-id}`
- Row value: `{base_commit_id}`

`{base_commit_id}` is the parent commit of this journal. Note that implicit
journals always have a single parent (merge commits cannot be implicit
journals).

## Head commits

The list of head commits is updated and maintained in storage. For each head a
separate row is created:
- Row key: `heads/{commit_id}`
- Row value: `{creation_timestamp}`

## Synchronization status

### Commits
A row is added for each commit that has been created locally, but is not yet
synced to the cloud:
- Row key: `unsynced/commits/{commit_id}`
- Row value: `{generation}`

### Value and Tree Node Objects
Each piece, i.e. part of a value or tree node object, can be in any of the
following states:

- transient: the object piece has been used in a journal that is not yet committed
- local: the object piece has been used in a commit, but is still not synced
- synced: the object piece has been synced to the cloud

For each piece, a status row is stored:

- Row key: `{status}/object_digests/{object_piece_identifier}`
- Row value: (empty value)

Where status is one of `transient`, `local`, or `synced`.

## Cloud sync metadata

The cloud sync component persists in storage rows with some metadata.

- Row key: `sync_metadata/{metadata_type}`
- Row value: `{metadata_value}`

Currently, cloud sync only stores a single such line, which contains the
server-side timestamp of the last commit fetched from the cloud.


# Pages metadata storage

Additionally to user-created content and metadata on this content, Ledger
persists information on Page usage, such as the timestamp of when each page was
last used. This information is used for page eviction, i.e. removing local
copies of pages, in order to free up device storage when that is necessary.

Page usage information is stored in a dedicated path: `{repo_dir}/page_usage_db`
using LevelDB.

## Timestamp of last usage
For each page that is locally stored on the device a row is created in the
underlying database:

- Row key: `opened/{ledger_name}{page_id}`
- Row value: `{timestamp}` or `{0}`

`{timestamp}` is the timestamp from when the given page was last closed. If the
page is currently open, the value is a 0 timestamp.

# See also

For more information see also:
 - [Life of a Put](life_of_a_put.md)
 - [Ledger Architecture - Storage](architecture.md#Storage)


[commit.fbs]: /bin/ledger/storage/impl/commit.fbs
[split.cc]: /bin/ledger/storage/impl/split.cc
[split.h]: /bin/ledger/storage/impl/split.h
[tree_node.fbs]: /bin/ledger/storage/impl/btree/tree_node.fbs
[journals]: life_of_a_put.md#Journals
