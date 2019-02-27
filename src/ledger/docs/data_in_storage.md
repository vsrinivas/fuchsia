# Data in Storage

Storage implements persistent representation of data held in Ledger. Such data
include:

- commit, value and tree node objects
- information on head commits
- information on which objects have been synced to the cloud
- other synchronization metadata, such as the time of the last synchronization
  to the cloud

All data and metadata added in storage are persisted using LevelDB. For each
[page](data_organization.md#Pages) a separate LevelDB instance is created in a
dedicated filesystem path of the form:
`{repo_dir}/{serialization_version}/ledgers/{ledger_dir}/{page_id_base64}/leveldb`.

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

## Head commits

The list of head commits is updated and maintained in storage. For each head a
separate row is created:
- Row key: `heads/{commit_id}`
- Row value: `{creation_timestamp}`

## Merge commits

For each pair of parent commits, the list of their merge commits is updated and
maintained in storage. For each merge with id `{merge_commit_id}` and parents
`{parent1_id}` and `{parent2_id}`, a separate row is created. The ids of the
parents are sorted so that `{parent1_id}` is less than `{parent2_id}` (this may
be a different order than given the commit object). Then the row is:
- Row key: `merges/{parent1_id}/{parent2_id}/{merge_commit_id}`
- Row value: (empty value)

## References

For the purpose of garbage collecting stale local objects, Ledger keeps a list
of references between objects, as well as from commits to objects.

### Object references

For each reference from a piece or object with digest `source_id` to a piece or
object with digest `destination_id`, a separate row is created. We define
`length` as a single byte encoding the size of `destination_id`, and `type` as
either `lazy` for references from a BTree node to a lazy value and `eager`
otherwise. Then the row is:
- Row key: `refcounts/{length}{destination_id}/object/{type}/{source_id}`
- Row value: (empty value)

### Commit references

For each reference from a commit with commit id `source_id` to a BTree node with
digest `destination_id`, a separate row is created. We define `length` as a
single byte encoding the size of `destination_id`. Then the row is:
- Row key: `refcounts/{length}{destination_id}/commit/{source_id}`
- Row value: (empty value)

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


[commit.fbs]: /src/ledger/bin/storage/impl/commit.fbs
[split.cc]: /src/ledger/bin/storage/impl/split.cc
[split.h]: /src/ledger/bin/storage/impl/split.h
[tree_node.fbs]: /src/ledger/bin/storage/impl/btree/tree_node.fbs
[journals]: life_of_a_put.md#Journals
