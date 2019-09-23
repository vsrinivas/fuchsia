# Split algorithm

The algorithm takes a stream of data and chunks it with the following
constraints:
- No generated chunk is bigger than 2^16-1 (64k-1) bytes
- Chunks have an expected size of 8k
- The algorithm attempts to avoid creating chunks smaller than 4k (but might
  create such chunks)

The algorithm produces a set of chunks. Each of the chunks can either be:
- A data chunk that is a sub-sequence of bytes from the original stream.
- An index chunk that contains a sequence of identifiers of chunks that need to
  be concatenated to produce the content of the chunk. These identifiers
  reference themselves either a data chunk or an index chunk.

The chunks are produced using the rolling hash algorithm H(), defined at
//peridot/third\_party/bup/bupsplit.h. Moreover, the result of this hashing
function is passed through a user defined permutation so that the result of this
split algorithm is not a signature of the initial file.

Given a sequence of bytes [b\_0, b\_1, b\_2, ..., b\_N], the algorithm computes
the smallest index i\_0, such that: i >= 4k and H([b\_0, ..., b\_{i\_0}]) 13 least
significant bits are 0.

If no such i\_0 exist, and N is lesser than 2^16, the algorithm just returns the
original data as the unique chunk, otherwise, i\_0 is choosen to be equals to
2^16-1.

The first chunk is set to be [b\_0, b\_1, ..., b\_{i\_0}].

The algorithm continues to find the other indices with the following properties:
i\_k is the smallest index such that:
- H([b\_0, b\_1, ..., b\_{i\_k}]) 13 least significant bits are 0.
- i\_k - i\_{k-1} >= 4k
- i\_k - i\_{k-1} < 16k

If so such i\_k exists, and N-i\_{k-1} is lesser than 2^16, i\_k is choosen to
be equals to N, and the index selection stops, otherwise, i\_k is choosen to be
i\_{k-1} + 2^16 -1

Once all indices are chosen, this indicates where the initial stream must be
chunked. Each chunk of data is then given an identifier, and the algorithm
produces chunks to encode the sequence of indices.

Given a sequence of indices [i\_0, i\_1, ..., i\_k], the algorithm needs to
decide where a sub-sequence of index must be turned into a chunk, indexed and
referenced by a higher level index. To decide this, the algorithm reuses the
value of the Hash that determined where to cut the chunk. The hash ends with its
13 least significant bits as 0, the algorithm examines the remaininig bits. For
each 4 least significant bits equals to 0, it increase the level of the index
by 1. For example:
- If the next 4 least significant bit are not all 0, a single identifier is
  added to the indices of level 0
- If the next 4 least significant bit are all 0, but not the 4 next ones, an
  identifier is added to the indices of level 0, then all current index of level 0
  are concatenated into a new chunk, and the identifier of this chunk is added
  to the indices of level 1.
- If the next 8 least significant bit are all 0, but not the 4 next ones, an
  identifier is added to the indices of level 0, then all current index of level
  0 are concatenated into a new chunk, and the identifier of this chunk is added
  to the indices of level 1, then all current index of level 1 are concatenated
  into a new chunk, and the identifier of this chunk is added to the indices of
  level 2.
- etc.

For this algorithm to be useful and efficient, the hash is chosen such that:
- The hash value only depends on the last 128 bytes of the data.
- With |w| being a sequence of bytes, a and b being bytes, H(w b) can be
  computed from H(a w), a and b.

These properties ensure that:
- It is possible to compute the hash of any prefix of a sequence in O(1)
- A change of a single byte in the sequence will change at most 2 chunks.
