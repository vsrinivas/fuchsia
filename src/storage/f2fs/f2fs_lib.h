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
inline void *PageAddress(Page &page) { return (void *)page.data; }
// TODO: impl
inline Page *FindGetPage(void *vnode, pgoff_t index) { return nullptr; }
inline int PageUptodate(struct Page *page) { return 0; }
inline void SetPageUptodate(struct Page *page) {}
inline void ClearPageUptodate(struct Page *page) {}
inline void ClearPagePrivate(struct Page *) {}
struct WritebackControl {};
inline int PageDirty(struct Page *) { return 0; }
// TODO: impl
inline int ClearPageDirtyForIo(Page *page) { return 0; }
inline void SetPageWriteback(Page *page) {}
inline void WaitOnPageWriteback(Page *page) {}

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

inline void SetBit(uint32_t nr, void *addr) {
  uint8_t *bitmap = static_cast<uint8_t *>(addr);
  uint32_t size_per_iter = kBitsPerByte;

  uint32_t iter = nr / size_per_iter;
  uint32_t offset_in_iter = nr % size_per_iter;

  bitmap[iter] |= static_cast<uint8_t>(1U << offset_in_iter);
}

inline void ClearBit(uint32_t nr, void *addr) {
  uint8_t *bitmap = static_cast<uint8_t *>(addr);
  uint32_t size_per_iter = kBitsPerByte;

  uint32_t iter = nr / size_per_iter;
  uint32_t offset_in_iter = nr % size_per_iter;

  bitmap[iter] &= ~static_cast<uint8_t>(1U << offset_in_iter);
}

inline bool TestBit(uint32_t nr, const void *addr) {
  const uint8_t *bitmap = static_cast<const uint8_t *>(addr);
  uint32_t size_per_iter = kBitsPerByte;

  uint32_t iter = nr / size_per_iter;
  uint32_t offset_in_iter = nr % size_per_iter;

  bool ret = ((bitmap[iter] & static_cast<uint8_t>(1U << offset_in_iter)) != 0);

  return ret;
}

inline uint32_t FindNextZeroBit(const void *addr, uint32_t size, uint32_t offset) {
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

inline uint32_t FindNextBit(const void *addr, uint32_t size, uint32_t offset) {
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

inline bool TestAndSetBit(uint32_t nr, void *addr) {
  uint8_t *bitmap = static_cast<uint8_t *>(addr);
  uint32_t size_per_iter = kBitsPerByte;
  uint32_t iter = nr / size_per_iter;
  uint32_t offset_in_iter = nr % size_per_iter;

  bool ret = ((bitmap[iter] & static_cast<uint8_t>(1U << offset_in_iter)) != 0);

  bitmap[iter] |= static_cast<uint8_t>(1U << offset_in_iter);

  return ret;
}

inline bool TestAndClearBit(uint32_t nr, void *addr) {
  uint8_t *bitmap = static_cast<uint8_t *>(addr);
  uint32_t size_per_iter = kBitsPerByte;

  uint32_t iter = nr / size_per_iter;
  uint32_t offset_in_iter = nr % size_per_iter;

  bool ret = ((bitmap[iter] & static_cast<uint8_t>(1U << offset_in_iter)) != 0);

  bitmap[iter] &= ~static_cast<uint8_t>((1U << offset_in_iter));

  return ret;
}

inline void list_add(list_node_t *list, list_node_t *item) {
  list->next->prev = item;
  item->next = list->next;
  item->prev = list;
  list->next = item;
}

inline void ZeroUserSegment(Page *page, uint32_t start, uint32_t end) {
  uint8_t *data = static_cast<uint8_t *>(PageAddress(page));

  ZX_ASSERT(end <= kPageSize && start <= kPageSize);

  if (end > start) {
    memset(data + start, 0, end - start);
  }
}

inline bool IsDotOrDotDot(std::string_view name) { return (name == "." || name == ".."); }

}  // namespace f2fs

#endif  // SRC_STORAGE_F2FS_F2FS_LIB_H_
