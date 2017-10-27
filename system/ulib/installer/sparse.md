# Sparse File Library

This library supports reading a sparse file and writing it into a compact format
for transport. The library also provides functions to restore the original file.
The operation of compacting the sparse file is referred to as 'sparsing', while
the restore operation is called 'unsparsing'. Currently the sparsing operation
requires that the input file be a multiple of 4K to support the possibility
that when the file is restored it may be written directly to a block device with
4K-size blocks.

The library defines a 'chunk_t' type which consists of two integer values which
describe an offset position in the file and a length. We refer to these as
'chunks' or 'header blocks'. The compacted sparse file consists of a header
block followed by run of data, followed by a header block, a run of data, etc.
The end of the compact file contains a special header block whose offset
position is 0 and whose length is set to the total size of the unsparsed file.
The format requires that header blocks be sorted in ascending order with respect
to their offset position. This allows the code to assume that after the first
block, if a zero-offset is encountered it is the end of the file. The header
blocks are expected ot be 16 bytes, but will vary based on the actual size of
off_t.

When unsparsing a file the space between data blocks is not written, but left
'empty' using seek operations. If the set of header blocks looked like
{{start:0, len:10}, {start: 26, len:4}, {start:30, len:4}, {start:0, len:40}}
the the written file would have holes from bytes offsets 10-25 and 34-39.
