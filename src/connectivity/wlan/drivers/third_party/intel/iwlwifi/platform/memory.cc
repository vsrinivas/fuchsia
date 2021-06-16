#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/platform/memory.h"

#include <lib/ddk/io-buffer.h>

#include <memory>

struct iwl_iobuf {
  io_buffer_t io_buffer = {};
};

zx_status_t iwl_iobuf_allocate_contiguous(struct device* dev, size_t size,
                                          struct iwl_iobuf** out_iobuf) {
  zx_status_t status = ZX_OK;
  auto iobuf = std::make_unique<struct iwl_iobuf>();
  if ((status = io_buffer_init(&iobuf->io_buffer, dev->bti, size,
                               IO_BUFFER_RW | IO_BUFFER_CONTIG)) != ZX_OK) {
    return status;
  }

  *out_iobuf = iobuf.release();
  return ZX_OK;
}

size_t iwl_iobuf_size(const struct iwl_iobuf* iobuf) {
  return io_buffer_size(&iobuf->io_buffer, 0);
}

void* iwl_iobuf_virtual(const struct iwl_iobuf* iobuf) { return io_buffer_virt(&iobuf->io_buffer); }

dma_addr_t iwl_iobuf_physical(const struct iwl_iobuf* iobuf) {
  return io_buffer_phys(&iobuf->io_buffer);
}

zx_status_t iwl_iobuf_cache_flush(struct iwl_iobuf* iobuf, size_t offset, size_t size) {
  return io_buffer_cache_flush(&iobuf->io_buffer, offset, size);
}

void iwl_iobuf_release(struct iwl_iobuf* iobuf) {
  io_buffer_release(&iobuf->io_buffer);
  delete iobuf;
}
