// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_DEV_INTERRUPT_INCLUDE_DEV_INTERRUPT_H_
#define ZIRCON_KERNEL_DEV_INTERRUPT_INCLUDE_DEV_INTERRUPT_H_

#include <stdbool.h>
#include <sys/types.h>
#include <zircon/compiler.h>
#include <zircon/types.h>

#include <kernel/mp.h>

__BEGIN_CDECLS

#define MAX_MSI_IRQS 32u

enum interrupt_trigger_mode {
  IRQ_TRIGGER_MODE_EDGE = 0,
  IRQ_TRIGGER_MODE_LEVEL = 1,
};

enum interrupt_polarity {
  IRQ_POLARITY_ACTIVE_HIGH = 0,
  IRQ_POLARITY_ACTIVE_LOW = 1,
};

zx_status_t mask_interrupt(unsigned int vector);
zx_status_t unmask_interrupt(unsigned int vector);
zx_status_t deactivate_interrupt(unsigned int vector);

void shutdown_interrupts(void);

// Shutdown interrupts for the calling CPU.
//
// Should be called before powering off the calling CPU.
void shutdown_interrupts_curr_cpu(void);

// Configure the specified interrupt vector.  If it is invoked, it muust be
// invoked prior to interrupt registration
zx_status_t configure_interrupt(unsigned int vector, enum interrupt_trigger_mode tm,
                                enum interrupt_polarity pol);

zx_status_t get_interrupt_config(unsigned int vector, enum interrupt_trigger_mode* tm,
                                 enum interrupt_polarity* pol);

typedef interrupt_eoi (*int_handler)(void* arg);

// Registers a handler+arg to be called for the given interrupt vector. The handler may be called
// with internal spinlocks held and should not itself call register_int_handler. This handler may
// be serialized with other handlers.
// This can be called repeatedly to change the handler/arg for a given vector.
zx_status_t register_int_handler(unsigned int vector, int_handler handler, void* arg);

// Registers a handler+arg to be called for the given interrupt vector. Once this is used to set a
// handler it is an error to modify the vector again through this or register_int_handler.
// Registration via this method allows the interrupt manager to avoid needing to synchronize
// re-registrations with invocations, which can be much more efficient and avoid unneeded
// serialization of handlers.
zx_status_t register_permanent_int_handler(unsigned int vector, int_handler handler, void* arg);

// These return the [base, max] range of vectors that can be used with zx_interrupt syscalls
// This api will need to evolve if valid vector ranges later are not contiguous
uint32_t interrupt_get_base_vector(void);
uint32_t interrupt_get_max_vector(void);

bool is_valid_interrupt(unsigned int vector, uint32_t flags);

unsigned int remap_interrupt(unsigned int vector);

// sends an inter-processor interrupt
void interrupt_send_ipi(cpu_mask_t target, mp_ipi_t ipi);

// performs per-cpu initialization for the interrupt controller
void interrupt_init_percpu(void);

// A structure which holds the state of a block of IRQs allocated by the
// platform to be used for delivering MSI or MSI-X interrupts.
typedef struct msi_block {
  void* platform_ctx;  // Allocation context owned by the platform
  uint64_t tgt_addr;   // The target write transaction physical address
  bool allocated;      // Whether or not this block has been allocated
  uint base_irq_id;    // The first IRQ id in the allocated block
  uint num_irq;        // The number of irqs in the allocated block

  // The data which the device should write when triggering an IRQ.  Note,
  // only the lower 16 bits are used when the block has been allocated for MSI
  // instead of MSI-X
  uint32_t tgt_data;
} msi_block_t;

// Methods used to determine if a platform supports MSI or not, and if so,
// whether or not the platform can mask individual MSI vectors at the
// platform level.
//
// If the platform supports MSI, it must supply valid implementations of
// msi_alloc_block, msi_free_block, and msi_register_handler.
//
// If the platform supports MSI masking, it must supply a valid
// implementation of MaskUnmaskMsi.
bool msi_is_supported(void);
bool msi_supports_masking(void);
void msi_mask_unmask(const msi_block_t* block, uint msi_id, bool mask);

// Method used for platform allocation of blocks of MSI and MSI-X compatible
// IRQ targets.
//
// @param requested_irqs The total number of irqs being requested.
// @param can_target_64bit True if the target address of the MSI block can
//        be located past the 4GB boundary.  False if the target address must be
//        in low memory.
// @param is_msix True if this request is for an MSI-X compatible block.  False
//        for plain old MSI.
// @param out_block A pointer to the allocation bookkeeping to be filled out
//        upon successful allocation of the requested block of IRQs.
//
// @return A status code indicating the success or failure of the operation.
zx_status_t msi_alloc_block(uint requested_irqs, bool can_target_64bit, bool is_msix,
                            msi_block_t* out_block);

// Method used to free a block of MSI IRQs previously allocated by msi_alloc_block().
// This does not unregister IRQ handlers.
//
// @param block A pointer to the block to be returned
void msi_free_block(msi_block_t* block);

// Register a handler function for a given msi_id within an msi_block_t. Passing a
// NULL handler will effectively unregister a handler for a given msi_id within the
// block.
void msi_register_handler(const msi_block_t* block, uint msi_id, int_handler handler, void* ctx);
__END_CDECLS

#endif  // ZIRCON_KERNEL_DEV_INTERRUPT_INCLUDE_DEV_INTERRUPT_H_
