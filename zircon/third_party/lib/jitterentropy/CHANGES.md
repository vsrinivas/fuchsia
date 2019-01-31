2.1.0:
 * Convert all __[u|s][32|64] into [uint|int][32|64]_t
 * Remove all code protected by #if defined(__KERNEL__) && !defined(MODULE)
 * Add JENT_PRIVATE_COMPILE: Enable flag during compile when
   compiling a private copy of the Jitter RNG
 * Remove unused statistical test code
 * Add FIPS 140-2 continuous self test code
 * threshold for init-time stuck test configurable with JENT_STUCK_INIT_THRES
   during compile time

2.0.1:
 * Invcation of stuck test during initalization

2.0.0:
 * Replace the XOR folding of a time delta with an LFSR -- the use of an
   LFSR is mathematically more sound for the argument to maintain entropy

1.2.0:
 * Use constant time operation of jent_stir_pool to prevent leaking
   timing information about RNG.
 * Make it compile on 32 bit archtectures

1.1.0:
 * start new numbering schema
 * update processing of bit that is deemed holding no entropy by heuristic:
   XOR it into pool without LSFR and bit rotation (reported and suggested
   by Kevin Fowler <kevpfowler@gmail.com>)

