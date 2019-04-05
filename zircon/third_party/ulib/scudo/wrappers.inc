//===-- wrappers.inc --------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef SCUDO_PREFIX
#error "Define SCUDO_PREFIX prior to including this file!"
#endif

// malloc-type functions have to be aligned to std::max_align_t. This is
// distinct from (1U << SCUDO_MIN_ALIGNMENT_LOG), since C++ new-type functions
// do not have to abide by the same requirement.
#ifndef SCUDO_MALLOC_ALIGNMENT
#define SCUDO_MALLOC_ALIGNMENT FIRST_32_SECOND_64(8, 16)
#endif

INTERFACE void *SCUDO_PREFIX(calloc)(size_t nmemb, size_t size) {
  scudo::uptr Product;
  if (UNLIKELY(scudo::checkForCallocOverflow(size, nmemb, &Product))) {
    // There doesn't appear to be a specific errno for this situation.
    if (SCUDO_ALLOCATOR.canReturnNull())
      return nullptr;
    scudo::reportCallocOverflow(nmemb, size);
  }
  return scudo::setErrnoOnNull(SCUDO_ALLOCATOR.allocate(
      Product, scudo::FromMalloc, SCUDO_MALLOC_ALIGNMENT, true));
}

INTERFACE void SCUDO_PREFIX(free)(void *ptr) {
  SCUDO_ALLOCATOR.deallocate(ptr, scudo::FromMalloc);
}

INTERFACE struct _mallinfo SCUDO_PREFIX(mallinfo)(void) {
  struct _mallinfo Info = {};
  return Info;
}

INTERFACE void *SCUDO_PREFIX(malloc)(size_t size) {
  return scudo::setErrnoOnNull(SCUDO_ALLOCATOR.allocate(
      size, scudo::FromMalloc, SCUDO_MALLOC_ALIGNMENT));
}

INTERFACE size_t SCUDO_PREFIX(malloc_usable_size)(void *ptr) {
  return SCUDO_ALLOCATOR.getUsableSize(ptr);
}

INTERFACE void *SCUDO_PREFIX(memalign)(size_t alignment, size_t size) {
  if (UNLIKELY(!scudo::isPowerOfTwo(alignment))) {
    errno = EINVAL;
    if (SCUDO_ALLOCATOR.canReturnNull())
      return nullptr;
    scudo::reportAlignmentNotPowerOfTwo(alignment);
  }
  return SCUDO_ALLOCATOR.allocate(size, scudo::FromMemalign, alignment);
}

INTERFACE int SCUDO_PREFIX(posix_memalign)(void **memptr, size_t alignment,
                                           size_t size) {
  if (UNLIKELY(scudo::checkPosixMemalignAlignment(alignment))) {
    if (!SCUDO_ALLOCATOR.canReturnNull())
      scudo::reportInvalidPosixMemalignAlignment(alignment);
    return EINVAL;
  }
  void *Ptr = SCUDO_ALLOCATOR.allocate(size, scudo::FromMemalign, alignment);
  if (UNLIKELY(!Ptr))
    return ENOMEM;
  *memptr = Ptr;
  return 0;
}

INTERFACE void *SCUDO_PREFIX(pvalloc)(size_t size) {
  const scudo::uptr PageSize = scudo::getPageSizeCached();
  if (UNLIKELY(scudo::checkForPvallocOverflow(size, PageSize))) {
    errno = ENOMEM;
    if (SCUDO_ALLOCATOR.canReturnNull())
      return nullptr;
    scudo::reportPvallocOverflow(size);
  }
  // pvalloc(0) should allocate one page.
  return scudo::setErrnoOnNull(SCUDO_ALLOCATOR.allocate(
      size ? scudo::roundUpTo(size, PageSize) : PageSize, scudo::FromMemalign,
      PageSize));
}

INTERFACE void *SCUDO_PREFIX(realloc)(void *ptr, size_t size) {
  if (!ptr)
    return scudo::setErrnoOnNull(SCUDO_ALLOCATOR.allocate(
        size, scudo::FromMalloc, SCUDO_MALLOC_ALIGNMENT));
  if (size == 0) {
    SCUDO_ALLOCATOR.deallocate(ptr, scudo::FromMalloc);
    return nullptr;
  }
  return scudo::setErrnoOnNull(
      SCUDO_ALLOCATOR.reallocate(ptr, size, SCUDO_MALLOC_ALIGNMENT));
}

INTERFACE void *SCUDO_PREFIX(valloc)(size_t size) {
  return scudo::setErrnoOnNull(SCUDO_ALLOCATOR.allocate(
      size, scudo::FromMemalign, scudo::getPageSizeCached()));
}

// Bionic wants a function named PREFIX_iterate and not PREFIX_malloc_iterate
// which is somewhat inconsistent with the rest, workaround that.
#if SCUDO_ANDROID && _BIONIC
#define SCUDO_ITERATE iterate
#else
#define SCUDO_ITERATE malloc_iterate
#endif

INTERFACE int SCUDO_PREFIX(SCUDO_ITERATE)(
    uintptr_t base, size_t size,
    void (*callback)(uintptr_t base, size_t size, void *arg), void *arg) {
  SCUDO_ALLOCATOR.iterateOverChunks(base, size, callback, arg);
  return 0;
}

INTERFACE void SCUDO_PREFIX(malloc_disable)() { SCUDO_ALLOCATOR.disable(); }

INTERFACE void SCUDO_PREFIX(malloc_enable)() { SCUDO_ALLOCATOR.enable(); }

INTERFACE int SCUDO_PREFIX(mallopt)(int param, int value) {
  if (param == M_DECAY_TIME) {
    // TODO(kostyak): set release_to_os_interval_ms accordingly.
    (void)value;
  } else if (param == M_PURGE) {
    SCUDO_ALLOCATOR.releaseToOS();
    return 1;
  }
  return 0;
}

INTERFACE void *SCUDO_PREFIX(aligned_alloc)(size_t alignment, size_t size) {
  if (UNLIKELY(scudo::checkAlignedAllocAlignmentAndSize(alignment, size))) {
    errno = EINVAL;
    if (SCUDO_ALLOCATOR.canReturnNull())
      return nullptr;
    scudo::reportInvalidAlignedAllocAlignment(alignment, size);
  }
  return scudo::setErrnoOnNull(
      SCUDO_ALLOCATOR.allocate(size, scudo::FromMalloc, alignment));
}

INTERFACE int SCUDO_PREFIX(malloc_info)(int, FILE*) {
  errno = ENOTSUP;
  return -1;
}

