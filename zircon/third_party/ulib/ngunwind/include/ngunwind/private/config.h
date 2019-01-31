/* Libunwind configuration.
   The genesis of this file is autoconf's config.h.in,
   but we don't use autoconf.

   A few potentially useful values have been kept, for now.  */

/* Block signals before mutex operations */
/* #undef CONFIG_BLOCK_SIGNALS */

/* Enable Debug Frame */
/* #undef CONFIG_DEBUG_FRAME */

/* Support for Microsoft ABI extensions */
/* #undef CONFIG_MSABI_SUPPORT */

/* Define to 1 if you want every memory access validated */
#define CONSERVATIVE_CHECKS 1

/* Define to 1 if you have the <atomic_ops.h> header file. */
/* #undef HAVE_ATOMIC_OPS_H */

/* Define if you have liblzma */
/* #undef HAVE_LZMA */

/* Defined if __sync atomics are available */
#define HAVE_SYNC_ATOMICS 1
