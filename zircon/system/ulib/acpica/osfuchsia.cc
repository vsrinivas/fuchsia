// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "zircon/system/ulib/acpica/osfuchsia.h"

#include <zircon/assert.h>
#include <zircon/process.h>
#include <zircon/status.h>
#include <zircon/syscalls.h>
#include <zircon/time.h>

#include <acpica/acpi.h>

#if !defined(__x86_64__) && !defined(__aarch64__)
#error "Unsupported architecture"
#endif

__WEAK zx_handle_t root_resource_handle;

#define UNIMPLEMENTED() ZX_PANIC("%s unimplemented", __func__)

/**
 * @brief Initialize the OSL subsystem.
 *
 * This function allows the OSL to initialize itself.  It is called during
 * intiialization of the ACPICA subsystem.
 *
 * @return Initialization status
 */
ACPI_STATUS AcpiOsInitialize() {
  auto status = AcpiTaskThreadStart();
  if (status != AE_OK) {
    return status;
  }

  status = AcpiIoPortSetup();
  if (status != AE_OK) {
    return status;
  }
  return AE_OK;
}

/**
 * @brief Terminate the OSL subsystem.
 *
 * This function allows the OSL to cleanup and terminate.  It is called during
 * termination of the ACPICA subsystem.
 *
 * @return Termination status
 */
ACPI_STATUS AcpiOsTerminate() {
  AcpiTaskThreadTerminate();

  return AE_OK;
}

/**
 * @brief Obtain the Root ACPI table pointer (RSDP).
 *
 * @return The physical address of the RSDP
 */
ACPI_PHYSICAL_ADDRESS AcpiOsGetRootPointer() {
  zx_paddr_t acpi_rsdp, smbios;
  zx_status_t zx_status = zx_pc_firmware_tables(root_resource_handle, &acpi_rsdp, &smbios);
  if (zx_status == ZX_OK && acpi_rsdp != 0) {
    return acpi_rsdp;
  }

  ACPI_PHYSICAL_ADDRESS TableAddress = 0;
  ACPI_STATUS status = AcpiFindRootPointer(&TableAddress);
  if (status != AE_OK) {
    return 0;
  }
  return TableAddress;
}

/**
 * @brief Allow the host OS to override a predefined ACPI object.
 *
 * @param PredefinedObject A pointer to a predefind object (name and initial
 *        value)
 * @param NewValue Where a new value for the predefined object is returned.
 *        NULL if there is no override for this object.
 *
 * @return Exception code that indicates success or reason for failure.
 */
ACPI_STATUS AcpiOsPredefinedOverride(const ACPI_PREDEFINED_NAMES* PredefinedObject,
                                     ACPI_STRING* NewValue) {
  *NewValue = NULL;
  return AE_OK;
}

/**
 * @brief Allow the host OS to override a firmware ACPI table via a logical
 *        address.
 *
 * @param ExistingTable A pointer to the header of the existing ACPI table
 * @param NewTable Where the pointer to the replacment table is returned.  The
 *        OSL returns NULL if no replacement is provided.
 *
 * @return Exception code that indicates success or reason for failure.
 */
ACPI_STATUS AcpiOsTableOverride(ACPI_TABLE_HEADER* ExistingTable, ACPI_TABLE_HEADER** NewTable) {
  *NewTable = NULL;
  return AE_OK;
}

/**
 * @brief Allow the host OS to override a firmware ACPI table via a physical
 *        address.
 *
 * @param ExistingTable A pointer to the header of the existing ACPI table
 * @param NewAddress Where the physical address of the replacment table is
 *        returned.  The OSL returns NULL if no replacement is provided.
 * @param NewLength Where the length of the replacement table is returned.
 *
 * @return Exception code that indicates success or reason for failure.
 */
ACPI_STATUS AcpiOsPhysicalTableOverride(ACPI_TABLE_HEADER* ExistingTable,
                                        ACPI_PHYSICAL_ADDRESS* NewAddress, UINT32* NewTableLength) {
  *NewAddress = 0;
  return AE_OK;
}

/**
 * @brief Allocate memory from the dynamic memory pool.
 *
 * @param Size Amount of memory to allocate.
 *
 * @return A pointer to the allocated memory. A NULL pointer is returned on
 *         error.
 */
void* AcpiOsAllocate(ACPI_SIZE Size) { return malloc(Size); }

/**
 * @brief Free previously allocated memory.
 *
 * @param Memory A pointer to the memory to be freed.
 */
void AcpiOsFree(void* Memory) { free(Memory); }

/**
 * @brief Obtain the ID of the currently executing thread.
 *
 * @return A unique non-zero value that represents the ID of the currently
 *         executing thread. The value -1 is reserved and must not be returned
 *         by this interface.
 */
static_assert(sizeof(ACPI_THREAD_ID) >= sizeof(zx_handle_t), "tid size");
ACPI_THREAD_ID AcpiOsGetThreadId() { return (uintptr_t)thrd_current(); }

/**
 * @brief Suspend the running task (course granularity).
 *
 * @param Milliseconds The amount of time to sleep, in milliseconds.
 */
void AcpiOsSleep(UINT64 Milliseconds) {
  if (Milliseconds > UINT32_MAX) {
    // If we're asked to sleep for a long time (>1.5 months), shorten it
    Milliseconds = UINT32_MAX;
  }
  zx_nanosleep(zx_deadline_after(ZX_MSEC(Milliseconds)));
}

/**
 * @brief Wait for a short amount of time (fine granularity).
 *
 * Execution of the running thread is not suspended for this time.
 *
 * @param Microseconds The amount of time to delay, in microseconds.
 */
void AcpiOsStall(UINT32 Microseconds) { zx_nanosleep(zx_deadline_after(ZX_USEC(Microseconds))); }

/**
 * @brief Read a value from a memory location.
 *
 * @param Address Memory address to be read.
 * @param Value A pointer to a location where the data is to be returned.
 * @param Width The memory width in bits, either 8, 16, 32, or 64.
 *
 * @return Exception code that indicates success or reason for failure.
 */
ACPI_STATUS AcpiOsReadMemory(ACPI_PHYSICAL_ADDRESS Address, UINT64* Value, UINT32 Width) {
  UNIMPLEMENTED();
  return AE_OK;
}

/**
 * @brief Write a value to a memory location.
 *
 * @param Address Memory address where data is to be written.
 * @param Value Data to be written to the memory location.
 * @param Width The memory width in bits, either 8, 16, 32, or 64.
 *
 * @return Exception code that indicates success or reason for failure.
 */
ACPI_STATUS AcpiOsWriteMemory(ACPI_PHYSICAL_ADDRESS Address, UINT64 Value, UINT32 Width) {
  UNIMPLEMENTED();
  return AE_OK;
}

/**
 * @brief A hook before writing sleep registers to enter the sleep state.
 *
 * @param Which sleep state to enter
 * @param Register A value
 * @param Register B value
 *
 * @return AE_CTRL_TERMINATE to skip further sleep register writes, otherwise AE_OK
 */

ACPI_STATUS AcpiOsEnterSleep(UINT8 SleepState, UINT32 RegaValue, UINT32 RegbValue) {
  // The upstream ACPICA code expects that AcpiHwLegacySleep() or AcpiHwExtendedSleep() is invoked
  // with interrupts disabled.  It requires this because the last steps of going to sleep is writing
  // to a few registers, flushing the caches (so we don't lose data if the caches are dropped), and
  // then writing to a register to enter the sleep.  If we were to take an interrupt after the cache
  // flush but before entering sleep, we could have inconsistent memory after waking up.

  // In Fuchsia, ACPICA runs in usermode and we don't expose a mechanism for it to disable
  // interrupts. For full shutdown (sleep state 5) this does not matter as any cache corruption
  // will be trumped by full power loss. For any other S state transitions via AcpiHwLegacySleep()
  // or AcpiHwExtendedSleep() we make a call to zx_system_powerctl to execute the necessary code in
  // the kernel where interrupts can be disabled.  This means that any call to this hook is from a
  // function which we do not support for S state transitions so we should return an error.
  if (SleepState == ACPI_STATE_S5) {
    return (AE_OK);
  } else {
    return (AE_ERROR);
  }
}

/**
 * @brief Formatted stream output.
 *
 * @param Format A standard print format string.
 * @param ...
 */
void ACPI_INTERNAL_VAR_XFACE AcpiOsPrintf(const char* Format, ...) {
  va_list argp;
  va_start(argp, Format);
  AcpiOsVprintf(Format, argp);
  va_end(argp);
}

/**
 * @brief Formatted stream output.
 *
 * @param Format A standard print format string.
 * @param Args A variable parameter list
 */
void AcpiOsVprintf(const char* Format, va_list Args) {
  // Only implement if ACPI_DEBUG_OUTPUT is defined, otherwise this causes
  // excess boot spew.
#ifdef ACPI_DEBUG_OUTPUT
  vprintf(Format, Args);
#endif
}

/**
 * @brief Get current value of the system timer
 *
 * @return The current value of the system timer in 100-ns units.
 */
UINT64 AcpiOsGetTimer() { return zx_clock_get_monotonic() / 100; }

/**
 * @brief Break to the debugger or display a breakpoint message.
 *
 * @param Function Signal to be sent to the host operating system.  Either
 *        ACPI_SIGNAL_FATAL or ACPI_SIGNAL_BREAKPOINT
 * @param Info Data associated with the signal; type depends on signal type.
 *
 * @return Exception code that indicates success or reason for failure.
 */
ACPI_STATUS AcpiOsSignal(UINT32 Function, void* Info) {
  UNIMPLEMENTED();
  return AE_OK;
}

/*
 * According to the the ACPI specification, section 5.2.10, the platform boot firmware aligns
 * the FACS (Firmware ACPI Control Structure) on a 64-byte boundary anywhere within the system’s
 * memory address space. This means we can assume the alignment when interacting with it.
 * Specifically we need to be able to manipulate the GlobalLock contained in the FACS table with
 * atomic operations, and these require aligned accesses.
 *
 * Although we know that the table will be aligned, to prevent the compiler from complaining we
 * use a wrapper struct to set the alignment attribute.
 */
struct AlignedFacs {
  ACPI_TABLE_FACS table;
} __attribute__((aligned(8)));

/* Setting the alignment should not have changed the size. */
static_assert(sizeof(AlignedFacs) == sizeof(ACPI_TABLE_FACS));

/* @brief Acquire the ACPI global lock
 *
 * Implementation for ACPI_ACQUIRE_GLOBAL_LOCK
 *
 * @param FacsPtr pointer to the FACS ACPI structure
 *
 * @return True if the lock was successfully acquired
 */
bool _acpica_acquire_global_lock(void* FacsPtr) {
  ZX_DEBUG_ASSERT(reinterpret_cast<uintptr_t>(FacsPtr) % 8 == 0);
  AlignedFacs* table = (AlignedFacs*)FacsPtr;
  uint32_t old_val, new_val, test_val;
  do {
    old_val = test_val = table->table.GlobalLock;
    new_val = old_val & ~ACPI_GLOCK_PENDING;
    // If the lock is owned, we'll mark it pending
    if (new_val & ACPI_GLOCK_OWNED) {
      new_val |= ACPI_GLOCK_PENDING;
    }
    new_val |= ACPI_GLOCK_OWNED;
    __atomic_compare_exchange_n(&table->table.GlobalLock, &old_val, new_val, false,
                                __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
  } while (old_val != test_val);

  /* If we're here, we either acquired the lock or marked it pending */
  return !(new_val & ACPI_GLOCK_PENDING);
}

/* @brief Release the ACPI global lock
 *
 * Implementation for ACPI_RELEASE_GLOBAL_LOCK
 *
 * @param FacsPtr pointer to the FACS ACPI structure
 *
 * @return True if there is someone waiting to acquire the lock
 */
bool _acpica_release_global_lock(void* FacsPtr) {
  // the FACS table is required to be 8 byte aligned, so sanity check with an assert but
  // otherwise we can just treat it as being aligned.
  ZX_DEBUG_ASSERT(reinterpret_cast<uintptr_t>(FacsPtr) % 8 == 0);
  AlignedFacs* table = (AlignedFacs*)FacsPtr;
  uint32_t old_val, new_val, test_val;
  do {
    old_val = test_val = table->table.GlobalLock;
    new_val = old_val & ~(ACPI_GLOCK_PENDING | ACPI_GLOCK_OWNED);
    __atomic_compare_exchange_n(&table->table.GlobalLock, &old_val, new_val, false,
                                __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
  } while (old_val != test_val);

  return !!(old_val & ACPI_GLOCK_PENDING);
}
