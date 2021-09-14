// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_F2FS_F2FS_LIB_H_
#define SRC_STORAGE_F2FS_F2FS_LIB_H_

namespace f2fs {

// Page cache helper
// TODO: Need to be changed once Pager is available
inline Page *GrabCachePage(void *vnode, uint32_t nid, pgoff_t index) {
  Page *page = new Page();
  page->index = index;
  page->host = vnode;
  page->host_nid = nid;
  return page;
}

inline void *PageAddress(Page *page) { return (void *)page->data; }
inline Page *FindGetPage(/* TODO pgoff_t*/ uint32_t index) { return nullptr; }
inline int PageUptodate(struct Page *page) { return 0; }
inline void SetPageUptodate(struct Page *page) {}
inline void ClearPageUptodate(struct Page *page) {}
inline void ClearPagePrivate(struct Page *) {}
inline int PageDirty(struct Page *) { /*TODO: IMPL: does Page has dirty bit?*/
  return 0;
}

static inline int ClearPageDirtyForIo(Page *page) { return 0; }

struct WritebackControl {};

static inline void SetPageWriteback(Page *page) {
  // TODO: Once Pager is available, it could be used before VMO writeback
}

static inline void WaitOnPageWriteback(Page *page) {
  // TODO: Once Pager is availabe, it could be used for wb synchronization
}

// Checkpoint
inline bool VerAfter(uint64_t a, uint64_t b) { return a > b; }

// CRC
inline uint32_t F2fsCalCrc32(uint32_t crc, void *buff, uint32_t len) {
  int i;
  unsigned char *p = static_cast<unsigned char *>(buff);
  while (len--) {
    crc ^= *p++;
    for (i = 0; i < 8; i++)
      crc = (crc >> 1) ^ ((crc & 1) ? kCrcPolyLe : 0);
  }
  return crc;
}

inline uint32_t F2fsCrc32(void *buff, uint32_t len) {
  return F2fsCalCrc32(kF2fsSuperMagic, (unsigned char *)buff, len);
}

inline bool F2fsCrcValid(uint32_t blk_crc, void *buff, uint32_t buff_size) {
  return F2fsCrc32(buff, buff_size) == blk_crc;
}

// Bitmap operations
inline size_t DivRoundUp(size_t n, size_t d) { return (((n) + (d)-1) / (d)); }
inline size_t BitsToLongs(size_t nr) { return DivRoundUp(nr, kBitsPerByte * sizeof(long)); }

static inline void SetBit(uint32_t nr, void *addr) {
  uint8_t *bitmap = static_cast<uint8_t *>(addr);
  uint32_t size_per_iter = kBitsPerByte;

  uint32_t iter = nr / size_per_iter;
  uint32_t offset_in_iter = nr % size_per_iter;

  bitmap[iter] |= static_cast<uint8_t>(1U << offset_in_iter);
}

static inline void ClearBit(uint32_t nr, void *addr) {
  uint8_t *bitmap = static_cast<uint8_t *>(addr);
  uint32_t size_per_iter = kBitsPerByte;

  uint32_t iter = nr / size_per_iter;
  uint32_t offset_in_iter = nr % size_per_iter;

  bitmap[iter] &= ~static_cast<uint8_t>(1U << offset_in_iter);
}

static inline bool TestBit(uint32_t nr, const void *addr) {
  const uint8_t *bitmap = static_cast<const uint8_t *>(addr);
  uint32_t size_per_iter = kBitsPerByte;

  uint32_t iter = nr / size_per_iter;
  uint32_t offset_in_iter = nr % size_per_iter;

  bool ret = ((bitmap[iter] & static_cast<uint8_t>(1U << offset_in_iter)) != 0);

  return ret;
}

static inline uint32_t FindNextZeroBit(const void *addr, uint32_t size, uint32_t offset) {
  const uint8_t *bitmap = static_cast<const uint8_t *>(addr);
  uint32_t size_per_iter = kBitsPerByte;

  while (offset < size) {
    uint32_t iter = offset / size_per_iter;
    uint32_t offset_in_iter = offset % size_per_iter;

    uint8_t mask = static_cast<uint8_t>(~0U << offset_in_iter);
    uint8_t res = bitmap[iter] & mask;
    if (res != mask) {  // found
      for (; offset_in_iter < size_per_iter; offset_in_iter++) {
        if ((bitmap[iter] & static_cast<uint8_t>(1U << offset_in_iter)) == 0) {
          return std::min(iter * size_per_iter + offset_in_iter, size);
        }
      }
    }

    offset = (iter + 1) * size_per_iter;
  }

  return size;
}

static inline uint32_t FindNextBit(const void *addr, uint32_t size, uint32_t offset) {
  const uint8_t *bitmap = static_cast<const uint8_t *>(addr);
  uint32_t size_per_iter = kBitsPerByte;

  while (offset < size) {
    uint32_t iter = offset / size_per_iter;
    uint32_t offset_in_iter = offset % size_per_iter;

    uint8_t mask = static_cast<uint8_t>(~0U << offset_in_iter);
    uint8_t res = bitmap[iter] & mask;
    if (res != 0) {  // found
      for (; offset_in_iter < size_per_iter; offset_in_iter++) {
        if ((bitmap[iter] & static_cast<uint8_t>(1U << offset_in_iter)) != 0) {
          return std::min(iter * size_per_iter + offset_in_iter, size);
        }
      }
    }

    offset = (iter + 1) * size_per_iter;
  }

  return size;
}

static inline bool TestAndSetBit(uint32_t nr, void *addr) {
  uint8_t *bitmap = static_cast<uint8_t *>(addr);
  uint32_t size_per_iter = kBitsPerByte;
  uint32_t iter = nr / size_per_iter;
  uint32_t offset_in_iter = nr % size_per_iter;

  bool ret = ((bitmap[iter] & static_cast<uint8_t>(1U << offset_in_iter)) != 0);

  bitmap[iter] |= static_cast<uint8_t>(1U << offset_in_iter);

  return ret;
}

static inline bool TestAndClearBit(uint32_t nr, void *addr) {
  uint8_t *bitmap = static_cast<uint8_t *>(addr);
  uint32_t size_per_iter = kBitsPerByte;

  uint32_t iter = nr / size_per_iter;
  uint32_t offset_in_iter = nr % size_per_iter;

  bool ret = ((bitmap[iter] & static_cast<uint8_t>(1U << offset_in_iter)) != 0);

  bitmap[iter] &= ~static_cast<uint8_t>((1U << offset_in_iter));

  return ret;
}

/*
 * Atomic wrapper
 */
static inline void AtomicSet(atomic_t *t, int value) {
  atomic_store_explicit(t, value, std::memory_order_relaxed);
}

static inline atomic_t AtomicRead(atomic_t *t) {
  uint32_t ret = atomic_load_explicit(t, std::memory_order_relaxed);
  return ret;
}

static inline void AtomicInc(atomic_t *t) {
  atomic_fetch_add_explicit(t, 1, std::memory_order_relaxed);
}

static inline void AtomicDec(atomic_t *t) {
  atomic_fetch_sub_explicit(t, 1, std::memory_order_relaxed);
}

// List operations
inline void list_move_tail(list_node_t *list, list_node_t *item) {
  list_delete(item);
  list_add_tail(list, item);
}

static inline void list_add(list_node_t *list, list_node_t *item) {
  list->next->prev = item;
  item->next = list->next;
  item->prev = list;
  list->next = item;
}

// Zero segment
static inline void ZeroUserSegments(Page *page, unsigned start1, unsigned end1, unsigned start2,
                                    unsigned end2) {
  char *data = (char *)PageAddress(page);

  ZX_ASSERT(end1 <= kPageSize && end2 <= kPageSize);

  if (end1 > start1)
    memset(data + start1, 0, end1 - start1);

  if (end2 > start2)
    memset(data + start2, 0, end2 - start2);
}

static inline void ZeroUserSegment(Page *page, unsigned start, unsigned end) {
  ZeroUserSegments(page, start, end, 0, 0);
}

static inline void zero_user(Page *page, unsigned start, unsigned size) {
  ZeroUserSegments(page, start, start + size, 0, 0);
}

// Inode
static inline void *Igrab(void *vnode) {
  // TODO: need to add ref. count if vnode is valid
  return vnode;
}

static inline void *Iput(void *vnode) {
  // TODO: need to decrement ref.
  // TODO  handle vnode according to its vaility when ref = 0
  return vnode;
}

static inline void ClearInode(void *vnode) {
  // TODO: IMPL according to Fuchsia vnode flags and state transition (e.g., I_FREEING | I_CLEAR)
}

static inline uint64_t DivU64(uint64_t dividend, uint32_t divisor) {
  return dividend / ((uint64_t)divisor);
}

}  // namespace f2fs

#endif  // SRC_STORAGE_F2FS_F2FS_LIB_H_
