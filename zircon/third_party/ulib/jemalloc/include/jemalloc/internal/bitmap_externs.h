#ifndef JEMALLOC_INTERNAL_BITMAP_EXTERNS_H
#define JEMALLOC_INTERNAL_BITMAP_EXTERNS_H

#pragma GCC visibility push(hidden)

void	bitmap_info_init(bitmap_info_t *binfo, size_t nbits);
void	bitmap_init(bitmap_t *bitmap, const bitmap_info_t *binfo);
size_t	bitmap_size(const bitmap_info_t *binfo);

#pragma GCC visibility pop

#endif /* JEMALLOC_INTERNAL_BITMAP_EXTERNS_H */
