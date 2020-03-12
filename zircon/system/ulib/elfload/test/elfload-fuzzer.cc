#include <lib/zx/vmar.h>
#include <lib/zx/vmo.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <zircon/assert.h>
#include <zircon/syscalls.h>

#include <elfload/elfload.h>

#define PAGE_SIZE 4096

static const size_t kMaxPhNum = 1024;
elf_phdr_t phdrs[kMaxPhNum];

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
  // vmar_allocate does not work with 0 size.
  if (size == 0)
    return 0;

  zx::vmo vmo;
  zx_status_t status = zx::vmo::create(size, 0 /* flags */, &vmo);
  if (status != ZX_OK) {
    return 0;
  }

  status = vmo.write(data, 0, size);
  if (status != ZX_OK) {
    return 0;
  }

  elf_load_header_t header;
  uintptr_t phoff;
  status = elf_load_prepare(vmo.get(), nullptr /* hdr_buf */, 0 /* buf_sz */, &header, &phoff);
  if (status != ZX_OK) {
    return 0;
  }
  if (header.e_phnum > kMaxPhNum) {
    return 0;
  }
  status = elf_load_read_phdrs(vmo.get(), phdrs, phoff, header.e_phnum);
  if (status != ZX_OK) {
    return 0;
  }

  uintptr_t interp_off;
  size_t interp_len;
  elf_load_find_interp(phdrs, header.e_phnum, &interp_off, &interp_len);

  zx_handle_t segments_vmar;
  status = elf_load_map_segments(zx::vmar::root_self()->get(), &header, phdrs, vmo.get(),
                                 &segments_vmar, nullptr /* base */, nullptr /* entry */);
  if (status != ZX_OK) {
    return 0;
  }

  zx_vmar_destroy(segments_vmar);
  zx_handle_close(segments_vmar);
  return 0;
}
