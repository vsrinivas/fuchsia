// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <assert.h>
#include <inttypes.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <threads.h>

#include <hw/inout.h>
#include <magenta/syscalls.h>

__WEAK mx_handle_t root_resource_handle;

#if !defined(__x86_64__) && !defined(__x86__)
#error "Unsupported architecture"
#endif

#include "acpi.h"

#define _COMPONENT          ACPI_OS_SERVICES
ACPI_MODULE_NAME    ("osmagenta")

#define LOCAL_TRACE 0

#define TRACEF(str, x...) do { printf("%s:%d: " str, __FUNCTION__, __LINE__, ## x); } while (0)
#define LTRACEF(x...) do { if (LOCAL_TRACE) { TRACEF(x); } } while (0)

/* Data used for implementing AcpiOsExecute and
 * AcpiOsWaitEventsComplete */
static mtx_t os_execute_lock = MTX_INIT;
static cnd_t os_execute_cond;
static int os_execute_tasks = 0;

static struct {
    void* ecam;
    size_t ecam_size;
    bool has_legacy;
    bool pci_probed;
} acpi_pci_tbl = { NULL, 0, false, false };

const size_t PCIE_MAX_DEVICES_PER_BUS = 32;
const size_t PCIE_MAX_FUNCTIONS_PER_DEVICE = 8;
const size_t PCIE_EXTENDED_CONFIG_SIZE = 4096;

// TODO(cja) The next major CL should move these into some common place so that
// PciConfig and userspace code can use them.
#define PCI_CONFIG_ADDRESS (0xCF8)
#define PCI_CONFIG_DATA    (0xCFC)
#define PCI_BDF_ADDR(bus, dev, func, off) \
    ((1 << 31) | ((bus & 0xFF) << 16) | ((dev & 0x1F) << 11) | ((func & 0x7) << 8) | (off & 0xFC))

static void pci_x86_pio_cfg_read(uint8_t bus, uint8_t dev, uint8_t func,
                                uint8_t offset, uint32_t* val, size_t width) {
        size_t shift = (offset & 0x3) * 8;

        if (shift + width > 32) {
            printf("ACPI: error, pio cfg read width %zu not aligned to reg %#2x\n", width, offset);
            return;
        }

        uint32_t addr = PCI_BDF_ADDR(bus, dev, func, offset);
        outpd(PCI_CONFIG_ADDRESS, addr);
        uint32_t tmp_val = inpd(PCI_CONFIG_DATA);
        uint32_t width_mask = (1 << width) - 1;

        // Align the read to the correct offset, then mask based on byte width
        *val = (tmp_val >> shift) & width_mask;
}

static void pci_x86_pio_cfg_write(uint16_t bus, uint16_t dev, uint16_t func,
                              uint8_t offset, uint32_t val, size_t width) {
        size_t shift = (offset & 0x3) * 8;
        uint32_t width_mask = (1 << width) - 1;
        uint32_t write_mask = width_mask << shift;

        if (shift + width > 32) {
            printf("ACPI: error, pio cfg write width %zu not aligned to reg %#2x\n", width, offset);
        }

        uint32_t addr = PCI_BDF_ADDR(bus, dev, func, offset);
        outpd(PCI_CONFIG_ADDRESS, addr);
        uint32_t tmp_val = inpd(PCI_CONFIG_DATA);

        val &= width_mask;
        tmp_val &= ~write_mask;
        tmp_val |= (val << shift);
        outpd(PCI_CONFIG_DATA, tmp_val);
}

// Standard MMIO configuration
static ACPI_STATUS acpi_pci_ecam_cfg_rw(ACPI_PCI_ID *PciId, uint32_t reg,
                                UINT64* val, uint32_t width, bool write) {

    size_t offset = PciId->Bus;
    offset *= PCIE_MAX_DEVICES_PER_BUS;
    offset += PciId->Device;
    offset *= PCIE_MAX_FUNCTIONS_PER_DEVICE;
    offset += PciId->Function;
    offset *= PCIE_EXTENDED_CONFIG_SIZE;

    if (offset >= acpi_pci_tbl.ecam_size) {
        printf("ACPI read/write config out of range\n");
        return AE_ERROR;
    }

    void *ptr = ((uint8_t *)acpi_pci_tbl.ecam) + offset + reg;
    if (write) {
        switch (width) {
            case 8:
                *((volatile uint8_t *)ptr) = *val;
                break;
            case 16:
                *((volatile uint16_t *)ptr) = *val;
                break;
            case 32:
                *((volatile uint32_t *)ptr) = *val;
                break;
            case 64:
                *((volatile uint64_t *)ptr) = *val;
                break;
            default:
                return AE_ERROR;
        }

    } else {
        switch (width) {
            case 8:
                *val = *((volatile uint8_t *)ptr);
                break;
            case 16:
                *val = *((volatile uint16_t *)ptr);
                break;
            case 32:
                *val = *((volatile uint32_t *)ptr);
                break;
            case 64:
                *val = *((volatile uint64_t *)ptr);
                break;
            default:
                return AE_ERROR;
        }
    }

    return AE_OK;
}

// x86 PIO configuration support
#if __x86_64__
static ACPI_STATUS acpi_pci_x86_pio_cfg_rw(ACPI_PCI_ID *PciId, uint32_t reg,
                                uint32_t* val, uint32_t width, bool write) {
    if (write) {
        pci_x86_pio_cfg_write(PciId->Bus, PciId->Device, PciId->Function, reg, *val, width);
    } else {
        pci_x86_pio_cfg_read(PciId->Bus, PciId->Device, PciId->Function, reg, val, width);
    }

    return AE_OK;
}
#endif

static mx_status_t acpi_probe_ecam(void) {
    // Look for MCFG and set up the ECAM pointer if we find it for PCIe
    // subsequent calls to this will use the existing ecam read
    ACPI_TABLE_HEADER* raw_table = NULL;
    ACPI_STATUS status = AcpiGetTable((char*)ACPI_SIG_MCFG, 1, &raw_table);
    if (status != AE_OK) {
        LTRACEF("ACPI: No MCFG table found.\n");
        return MX_ERR_NOT_FOUND;
    }

    ACPI_TABLE_MCFG* mcfg = (ACPI_TABLE_MCFG*)raw_table;
    ACPI_MCFG_ALLOCATION* table_start = ((void*)mcfg) + sizeof(*mcfg);
    ACPI_MCFG_ALLOCATION* table_end = ((void*)mcfg) + mcfg->Header.Length;
    uintptr_t table_bytes = (uintptr_t)table_end - (uintptr_t)table_start;
    if (table_bytes % sizeof(*table_start) != 0) {
        LTRACEF("PCIe error, MCFG has unexpected size.\n");
        return MX_ERR_NOT_FOUND;
    }

    int num_entries = table_end - table_start;
    if (num_entries == 0) {
        LTRACEF("PCIe error, MCFG has no entries.\n");
        return MX_ERR_NOT_FOUND;
    }
    if (num_entries > 1) {
        LTRACEF("PCIe MCFG has more than one entry, using the first.\n");
    }

    const size_t size_per_bus = PCIE_EXTENDED_CONFIG_SIZE *
        PCIE_MAX_DEVICES_PER_BUS * PCIE_MAX_FUNCTIONS_PER_DEVICE;
    int num_buses = table_start->EndBusNumber - table_start->StartBusNumber + 1;

    if (table_start->PciSegment != 0) {
        LTRACEF("PCIe error, non-zero segment found.\n");
        return MX_ERR_NOT_FOUND;
    }

    uint8_t bus_start = table_start->StartBusNumber;
    if (bus_start != 0) {
        LTRACEF("PCIe error, non-zero bus start found.");
        return MX_ERR_NOT_FOUND;
    }

    // We need to adjust the physical address we received to align to the proper
    // bus number.
    //
    // Citation from PCI Firmware Spec 3.0:
    // For PCI-X and PCI Express platforms utilizing the enhanced
    // configuration access method, the base address of the memory mapped
    // configuration space always corresponds to bus number 0 (regardless
    // of the start bus number decoded by the host bridge).
    uint64_t base = table_start->Address + size_per_bus * bus_start;
    // The size of this mapping is defined in the PCI Firmware v3 spec to be
    // big enough for all of the buses in this config.
    acpi_pci_tbl.ecam_size = size_per_bus * num_buses;
    mx_status_t ret = mx_mmap_device_memory(root_resource_handle, base, acpi_pci_tbl.ecam_size,
                                            MX_CACHE_POLICY_UNCACHED_DEVICE,
                                            (uintptr_t *)&acpi_pci_tbl.ecam);
    if (ret == MX_OK && LOCAL_TRACE) {
        printf("ACPI: Found PCIe and mapped at %p.\n", acpi_pci_tbl.ecam);
    }

    return MX_OK;
}

static mx_status_t acpi_probe_legacy_pci(void) {
    mx_status_t status = MX_ERR_NOT_FOUND;
#if __x86_64__
    // Check for a Legacy PCI root complex at 00:00:0. For now, this assumes we
    // only care about segment 0. We'll cross that segmented bridge when we
    // come to it.
    uint16_t vendor_id;
    pci_x86_pio_cfg_read(0, 0, 0, 0, (uint32_t*)&vendor_id, 16);
    if (vendor_id != 0xFFFF) {
        acpi_pci_tbl.has_legacy = true;
        printf("ACPI: Found legacy PCI.\n");
        status = MX_OK;
    }
#endif

    return status;
}

static ACPI_STATUS thrd_status_to_acpi_status(int status) {
    switch (status) {
        case thrd_success: return AE_OK;
        case thrd_nomem: return AE_NO_MEMORY;
        case thrd_timedout: return AE_TIME;
        default: return AE_ERROR;
    }
}

/**
 * @brief Initialize the OSL subsystem.
 *
 * This function allows the OSL to initialize itself.  It is called during
 * intiialization of the ACPICA subsystem.
 *
 * @return Initialization status
 */
ACPI_STATUS AcpiOsInitialize() {
    ACPI_STATUS status = thrd_status_to_acpi_status(
            cnd_init(&os_execute_cond));
    if (status != AE_OK) {
        return status;
    }
    /* TODO(teisenbe): be less permissive */
    mx_mmap_device_io(root_resource_handle, 0, 65536);
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
    cnd_destroy(&os_execute_cond);

    return AE_OK;
}

/**
 * @brief Obtain the Root ACPI table pointer (RSDP).
 *
 * @return The physical address of the RSDP
 */
ACPI_PHYSICAL_ADDRESS AcpiOsGetRootPointer() {
    ACPI_PHYSICAL_ADDRESS TableAddress = 0;
    ACPI_STATUS status = AcpiFindRootPointer(&TableAddress);

    uint32_t uefi_rsdp = mx_acpi_uefi_rsdp(root_resource_handle);
    if (uefi_rsdp != 0) {
        return uefi_rsdp;
    }

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
ACPI_STATUS AcpiOsPredefinedOverride(
        const ACPI_PREDEFINED_NAMES *PredefinedObject,
        ACPI_STRING *NewValue) {
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
ACPI_STATUS AcpiOsTableOverride(
        ACPI_TABLE_HEADER *ExistingTable,
        ACPI_TABLE_HEADER **NewTable) {
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
ACPI_STATUS AcpiOsPhysicalTableOverride(
        ACPI_TABLE_HEADER *ExistingTable,
        ACPI_PHYSICAL_ADDRESS *NewAddress,
        UINT32 *NewTableLength) {
    *NewAddress = 0;
    return AE_OK;
}

// If we decide to make use of a more Magenta specific cache mechanism,
// remove the ACPI_USE_LOCAL_CACHE define from the header and implement these
// functions.
#if 0
/**
 * @brief Create a memory cache object.
 *
 * @param CacheName An ASCII identfier for the cache.
 * @param ObjectSize The size of each object in the cache.
 * @param MaxDepth Maximum number of objects in the cache.
 * @param ReturnCache Where a pointer to the cache object is returned.
 *
 * @return AE_OK The cache was successfully created.
 * @return AE_BAD_PARAMETER The ReturnCache pointer is NULL or ObjectSize < 16.
 * @return AE_NO_MEMORY Insufficient dynamic memory to complete the operation.
 */
ACPI_STATUS AcpiOsCreateCache(
        char *CacheName,
        UINT16 ObjectSize,
        UINT16 MaxDepth,
        ACPI_CACHE_T **ReturnCache) {
    PANIC_UNIMPLEMENTED;
    return AE_NO_MEMORY;
}

/**
 * @brief Delete a memory cache object.
 *
 * @param Cache The cache object to be deleted.
 *
 * @return AE_OK The cache was successfully deleted.
 * @return AE_BAD_PARAMETER The Cache pointer is NULL.
 */
ACPI_STATUS AcpiOsDeleteCache(ACPI_CACHE_T *Cache) {
    PANIC_UNIMPLEMENTED;
    return AE_OK;
}

/**
 * @brief Free all objects currently within a cache object.
 *
 * @param Cache The cache object to purge.
 *
 * @return AE_OK The cache was successfully purged.
 * @return AE_BAD_PARAMETER The Cache pointer is NULL.
 */
ACPI_STATUS AcpiOsPurgeCache(ACPI_CACHE_T *Cache) {
    PANIC_UNIMPLEMENTED;
    return AE_OK;
}


/**
 * @brief Acquire an object from a cache.
 *
 * @param Cache The cache object from which to acquire an object.
 *
 * @return A pointer to a cache object. NULL if the object could not be
 *         acquired.
 */
void *AcpiOsAcquireObject(ACPI_CACHE_T *Cache) {
    PANIC_UNIMPLEMENTED;
    return NULL;
}

/**
 * @brief Release an object to a cache.
 *
 * @param Cache The cache object to which the object will be released.
 * @param Object The object to be released.
 *
 * @return AE_OK The object was successfully released.
 * @return AE_BAD_PARAMETER The Cache or Object pointer is NULL.
 */
ACPI_STATUS AcpiOsReleaseObject(ACPI_CACHE_T *Cache, void *Object) {
    PANIC_UNIMPLEMENTED;
    return AE_OK;
}
#endif

/**
 * @brief Map physical memory into the caller's address space.
 *
 * @param PhysicalAddress A full physical address of the memory to be mapped
 *        into the caller's address space
 * @param Length The amount of memory to mapped starting at the given physical
 *        address
 *
 * @return Logical pointer to the mapped memory. A NULL pointer indicated failures.
 */
void *AcpiOsMapMemory(
        ACPI_PHYSICAL_ADDRESS PhysicalAddress,
        ACPI_SIZE Length) {

    // Caution: PhysicalAddress might not be page-aligned, Length might not
    // be a page multiple.

    ACPI_PHYSICAL_ADDRESS aligned_address = PhysicalAddress & ~(PAGE_SIZE - 1);
    ACPI_PHYSICAL_ADDRESS end = (PhysicalAddress + Length + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);

    uintptr_t vaddr;
    // TODO(teisenbe): Replace this with a VMO-based system
    mx_status_t status = mx_mmap_device_memory(root_resource_handle,
                                               aligned_address, end - aligned_address,
                                               MX_CACHE_POLICY_CACHED, &vaddr);
    if (status != MX_OK) {
        return NULL;
    }

    return (void*)(vaddr + (PhysicalAddress - aligned_address));
}

/**
 * @brief Remove a physical to logical memory mapping.
 *
 * @param LogicalAddress The logical address that was returned from a previous
 *        call to AcpiOsMapMemory.
 * @param Length The amount of memory that was mapped. This value must be
 *        identical to the value used in the call to AcpiOsMapMemory.
 */
void AcpiOsUnmapMemory(void *LogicalAddress, ACPI_SIZE Length) {
    // TODO(teisenbe): Implement
}

/**
 * @brief Allocate memory from the dynamic memory pool.
 *
 * @param Size Amount of memory to allocate.
 *
 * @return A pointer to the allocated memory. A NULL pointer is returned on
 *         error.
 */
void *AcpiOsAllocate(ACPI_SIZE Size) {
    return malloc(Size);
}

/**
 * @brief Free previously allocated memory.
 *
 * @param Memory A pointer to the memory to be freed.
 */
void AcpiOsFree(void *Memory) {
    free(Memory);
}

/**
 * @brief Obtain the ID of the currently executing thread.
 *
 * @return A unique non-zero value that represents the ID of the currently
 *         executing thread. The value -1 is reserved and must not be returned
 *         by this interface.
 */
static_assert(sizeof(ACPI_THREAD_ID) >= sizeof(mx_handle_t), "tid size");
ACPI_THREAD_ID AcpiOsGetThreadId() {
    return (uintptr_t)thrd_current();
}

/* Structures used for implementing AcpiOsExecute and
 * AcpiOsWaitEventsComplete */
struct acpi_os_task_ctx {
    ACPI_OSD_EXEC_CALLBACK func;
    void *ctx;
};

static int acpi_os_task(void *raw_ctx) {
    struct acpi_os_task_ctx *ctx = raw_ctx;

    ctx->func(ctx->ctx);

    mtx_lock(&os_execute_lock);
    os_execute_tasks--;
    if (os_execute_tasks == 0) {
        cnd_broadcast(&os_execute_cond);
    }
    mtx_unlock(&os_execute_lock);

    free(ctx);
    return 0;
}

/**
 * @brief Schedule a procedure for deferred execution.
 *
 * @param Type Type of the callback function.
 * @param Function Address of the procedure to execute.
 * @param Context A context value to be passed to the called procedure.
 *
 * @return AE_OK The procedure was successfully queued for execution.
 * @return AE_BAD_PARAMETER The Type is invalid or the Function pointer
 *         is NULL.
 */
ACPI_STATUS AcpiOsExecute(
        ACPI_EXECUTE_TYPE Type,
        ACPI_OSD_EXEC_CALLBACK Function,
        void *Context) {

    if (Function == NULL) {
        return AE_BAD_PARAMETER;
    }

    switch (Type) {
        case OSL_GLOBAL_LOCK_HANDLER:
        case OSL_NOTIFY_HANDLER:
        case OSL_GPE_HANDLER:
        case OSL_DEBUGGER_MAIN_THREAD:
        case OSL_DEBUGGER_EXEC_THREAD:
        case OSL_EC_POLL_HANDLER:
        case OSL_EC_BURST_HANDLER: break;
        default: return AE_BAD_PARAMETER;
    }

    struct acpi_os_task_ctx *ctx = malloc(sizeof(*ctx));
    if (!ctx) {
        return AE_NO_MEMORY;
    }
    ctx->func = Function;
    ctx->ctx = Context;

    mtx_lock(&os_execute_lock);
    os_execute_tasks++;
    mtx_unlock(&os_execute_lock);

    // TODO(teisenbe): Instead of spawning a thread each time for this,
    // we should back this with a thread pool.
    thrd_t thread;
    ACPI_STATUS status = thrd_status_to_acpi_status(
            thrd_create(&thread, acpi_os_task, ctx));
    if (status != AE_OK) {
        free(ctx);
        mtx_lock(&os_execute_lock);
        os_execute_tasks--;
        if (os_execute_tasks == 0) {
            cnd_broadcast(&os_execute_cond);
        }
        mtx_unlock(&os_execute_lock);
        return status;
    }

    thrd_detach(thread);
    return AE_OK;
}

/**
 * @brief Wait for completion of asynchronous events.
 *
 * This function blocks until all asynchronous events initiated by
 * AcpiOsExecute have completed.
 */
void AcpiOsWaitEventsComplete(void) {
    mtx_lock(&os_execute_lock);
    while (os_execute_tasks > 0) {
        cnd_wait(&os_execute_cond, &os_execute_lock);
    }
    mtx_unlock(&os_execute_lock);
}

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
    mx_nanosleep(mx_deadline_after(MX_MSEC(Milliseconds)));
}

/**
 * @brief Wait for a short amount of time (fine granularity).
 *
 * Execution of the running thread is not suspended for this time.
 *
 * @param Microseconds The amount of time to delay, in microseconds.
 */
void AcpiOsStall(UINT32 Microseconds) {
    mx_nanosleep(mx_deadline_after(MX_USEC(Microseconds)));
}

/**
 * @brief Create a semaphore.
 *
 * @param MaxUnits The maximum number of units this semaphore will be required
 *        to accept
 * @param InitialUnits The initial number of units to be assigned to the
 *        semaphore.
 * @param OutHandle A pointer to a locaton where a handle to the semaphore is
 *        to be returned.
 *
 * @return AE_OK The semaphore was successfully created.
 * @return AE_BAD_PARAMETER The InitialUnits is invalid or the OutHandle
 *         pointer is NULL.
 * @return AE_NO_MEMORY Insufficient memory to create the semaphore.
 */
ACPI_STATUS AcpiOsCreateSemaphore(
        UINT32 MaxUnits,
        UINT32 InitialUnits,
        ACPI_SEMAPHORE *OutHandle) {
    sem_t *sem = malloc(sizeof(sem_t));
    if (!sem) {
        return AE_NO_MEMORY;
    }
    if (sem_init(sem, 0, InitialUnits) < 0) {
        free(sem);
        return AE_ERROR;
    }
    *OutHandle = sem;
    return AE_OK;
}

/**
 * @brief Delete a semaphore.
 *
 * @param Handle A handle to a semaphore objected that was returned by a
 *        previous call to AcpiOsCreateSemaphore.
 *
 * @return AE_OK The semaphore was successfully deleted.
 * @return AE_BAD_PARAMETER The Handle is invalid.
 */
ACPI_STATUS AcpiOsDeleteSemaphore(ACPI_SEMAPHORE Handle) {
    free(Handle);
    return AE_OK;
}

/**
 * @brief Wait for units from a semaphore.
 *
 * @param Handle A handle to a semaphore objected that was returned by a
 *        previous call to AcpiOsCreateSemaphore.
 * @param Units The number of units the caller is requesting.
 * @param Timeout How long the caller is willing to wait for the requested
 *        units, in milliseconds.  A value of -1 indicates that the caller
 *        is willing to wait forever. Timeout may be 0.
 *
 * @return AE_OK The requested units were successfully received.
 * @return AE_BAD_PARAMETER The Handle is invalid.
 * @return AE_TIME The units could not be acquired within the specified time.
 */
ACPI_STATUS AcpiOsWaitSemaphore(
        ACPI_SEMAPHORE Handle,
        UINT32 Units,
        UINT16 Time) {
    // TODO(teisenbe): Implement this when we can calculate an absolute time to wait for
    // sem_timedwait
    if (sem_wait(Handle) < 0) {
        return AE_ERROR;
    }
    return AE_OK;
}

/**
 * @brief Send units to a semaphore.
 *
 * @param Handle A handle to a semaphore objected that was returned by a
 *        previous call to AcpiOsCreateSemaphore.
 * @param Units The number of units to send to the semaphore.
 *
 * @return AE_OK The semaphore was successfully signaled.
 * @return AE_BAD_PARAMETER The Handle is invalid.
 */
ACPI_STATUS AcpiOsSignalSemaphore(
        ACPI_SEMAPHORE Handle,
        UINT32 Units) {
    // TODO: Implement support for Units > 1
    assert(Units == 1);

    sem_post(Handle);
    return AE_OK;
}

/**
 * @brief Create a spin lock.
 *
 * @param OutHandle A pointer to a locaton where a handle to the lock is
 *        to be returned.
 *
 * @return AE_OK The lock was successfully created.
 * @return AE_BAD_PARAMETER The OutHandle pointer is NULL.
 * @return AE_NO_MEMORY Insufficient memory to create the lock.
 */
ACPI_STATUS AcpiOsCreateLock(ACPI_SPINLOCK *OutHandle) {
    // Since we don't have a notion of interrupt contex in usermode, just make
    // these mutexes.
    mtx_t* lock = malloc(sizeof(mtx_t));
    if (!lock) {
        return AE_NO_MEMORY;
    }

    ACPI_STATUS status = thrd_status_to_acpi_status(
            mtx_init(lock, mtx_plain));
    if (status != AE_OK) {
        return status;
    }
    *OutHandle = lock;
    return AE_OK;
}

/**
 * @brief Delete a spin lock.
 *
 * @param Handle A handle to a lock objected that was returned by a
 *        previous call to AcpiOsCreateLock.
 *
 * @return AE_OK The lock was successfully deleted.
 * @return AE_BAD_PARAMETER The Handle is invalid.
 */
void AcpiOsDeleteLock(ACPI_SPINLOCK Handle) {
    mtx_destroy(Handle);
    free(Handle);
}

/**
 * @brief Acquire a spin lock.
 *
 * @param Handle A handle to a lock objected that was returned by a
 *        previous call to AcpiOsCreateLock.
 *
 * @return Platform-dependent CPU flags.  To be used when the lock is released.
 */
ACPI_CPU_FLAGS AcpiOsAcquireLock(ACPI_SPINLOCK Handle) {
    mtx_lock(Handle);
    return 0;
}

/**
 * @brief Release a spin lock.
 *
 * @param Handle A handle to a lock objected that was returned by a
 *        previous call to AcpiOsCreateLock.
 * @param Flags CPU Flags that were returned from AcpiOsAcquireLock.
 */
void AcpiOsReleaseLock(ACPI_SPINLOCK Handle, ACPI_CPU_FLAGS Flags) {
    mtx_unlock(Handle);
}

// Wrapper structs for interfacing between our interrupt handler convention and
// ACPICA's
struct acpi_irq_thread_arg {
    ACPI_OSD_HANDLER handler;
    mx_handle_t irq_handle;
    void *context;
};
static int acpi_irq_thread(void *arg) {
    struct acpi_irq_thread_arg *real_arg = (struct acpi_irq_thread_arg *)arg;
    while (1) {
        mx_status_t status = mx_interrupt_wait(real_arg->irq_handle);
        if (status != MX_OK) {
            continue;
        }

        // TODO: Should we do something with the return value from the handler?
        real_arg->handler(real_arg->context);

        mx_interrupt_complete(real_arg->irq_handle);
    }
    return 0;
}

/**
 * @brief Install a handler for a hardware interrupt.
 *
 * @param InterruptLevel Interrupt level that the handler will service.
 * @param Handler Address of the handler.
 * @param Context A context value that is passed to the handler when the
 *        interrupt is dispatched.
 *
 * @return AE_OK The handler was successfully installed.
 * @return AE_BAD_PARAMETER The InterruptNumber is invalid or the Handler
 *         pointer is NULL.
 * @return AE_ALREADY_EXISTS A handler for this interrupt level is already
 *         installed.
 */
ACPI_STATUS AcpiOsInstallInterruptHandler(
        UINT32 InterruptLevel,
        ACPI_OSD_HANDLER Handler,
        void *Context) {
    // Note that InterruptLevel here is ISA IRQs (or global of the legacy PIC
    // does't exist), not system exceptions.

    // TODO: Clean this up to be less x86 centric.

    if (InterruptLevel == 0) {
        /* Some buggy firmware fails to populate the SCI_INT field of the FADT
         * properly.  0 is a known bad value, since the legacy PIT uses it and
         * cannot be remapped.  Just lie and say we installed a handler; this
         * system will just never receive an SCI.  If we return an error here,
         * ACPI init will fail completely, and the system will be unusable. */
        return AE_OK;
    }

    assert(InterruptLevel == 0x9); // SCI

    struct acpi_irq_thread_arg *arg = malloc(sizeof(*arg));
    if (!arg) {
        return AE_NO_MEMORY;
    }

    mx_handle_t handle = mx_interrupt_create(root_resource_handle, InterruptLevel, MX_FLAG_REMAP_IRQ);
    if (handle < 0) {
        free(arg);
        return AE_ERROR;
    }

    arg->handler = Handler;
    arg->context = Context;
    arg->irq_handle = handle;

    thrd_t thread;
    int ret = thrd_create(&thread, acpi_irq_thread, arg);
    if (ret != 0) {
        free(arg);
        return AE_ERROR;
    }
    thrd_detach(thread);

    return AE_OK;
}

/**
 * @brief Remove an interrupt handler.
 *
 * @param InterruptNumber Interrupt number that the handler is currently
 *        servicing.
 * @param Handler Address of the handler that was previously installed.
 *
 * @return AE_OK The handler was successfully removed.
 * @return AE_BAD_PARAMETER The InterruptNumber is invalid, the Handler
 *         pointer is NULL, or the Handler address is no the same as the one
 *         currently installed.
 * @return AE_NOT_EXIST There is no handler installed for this interrupt level.
 */
ACPI_STATUS AcpiOsRemoveInterruptHandler(
        UINT32 InterruptNumber,
        ACPI_OSD_HANDLER Handler) {
    assert(false);
    return AE_NOT_EXIST;
}

/**
 * @brief Read a value from a memory location.
 *
 * @param Address Memory address to be read.
 * @param Value A pointer to a location where the data is to be returned.
 * @param Width The memory width in bits, either 8, 16, 32, or 64.
 *
 * @return Exception code that indicates success or reason for failure.
 */
ACPI_STATUS AcpiOsReadMemory(
        ACPI_PHYSICAL_ADDRESS Address,
        UINT64 *Value,
        UINT32 Width) {
    assert(false);
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
ACPI_STATUS AcpiOsWriteMemory(
        ACPI_PHYSICAL_ADDRESS Address,
        UINT64 Value,
        UINT32 Width) {
    assert(false);
    return AE_OK;
}

/**
 * @brief Read a value from an input port.
 *
 * @param Address Hardware I/O port address to be read.
 * @param Value A pointer to a location where the data is to be returned.
 * @param Width The port width in bits, either 8, 16, or 32.
 *
 * @return Exception code that indicates success or reason for failure.
 */
ACPI_STATUS AcpiOsReadPort(
        ACPI_IO_ADDRESS Address,
        UINT32 *Value,
        UINT32 Width) {
    if (Address > 0xffff) {
        return AE_BAD_PARAMETER;
    }

    switch (Width) {
        case 8:
            *Value = inp(Address);
            break;
        case 16:
            *Value = inpw(Address);
            break;
        case 32:
            *Value = inpd(Address);
            break;
        default:
            return AE_BAD_PARAMETER;
    }
    return AE_OK;
}

/**
 * @brief Write a value to an output port.
 *
 * @param Address Hardware I/O port address where data is to be written.
 * @param Value The value to be written.
 * @param Width The port width in bits, either 8, 16, or 32.
 *
 * @return Exception code that indicates success or reason for failure.
 */
ACPI_STATUS AcpiOsWritePort(
        ACPI_IO_ADDRESS Address,
        UINT32 Value,
        UINT32 Width) {
    if (Address > 0xffff) {
        return AE_BAD_PARAMETER;
    }

    switch (Width) {
        case 8:
            outp(Address, (uint8_t)Value);
            break;
        case 16:
            outpw(Address, (uint16_t)Value);
            break;
        case 32:
            outpd(Address, (uint32_t)Value);
            break;
        default:
            return AE_BAD_PARAMETER;
    }
    return AE_OK;
}

/**
 * @brief Read/Write a value from a PCI configuration register.
 *
 * @param PciId The full PCI configuration space address, consisting of a
 *        segment number, bus number, device number, and function number.
 * @param Register The PCI register address to be read from.
 * @param Value A pointer to a location where the data is to be returned.
 * @param Width The register width in bits, either 8, 16, 32, or 64.
 * @param Write Write or Read.
 *
 * @return Exception code that indicates success or reason for failure.
 */
static ACPI_STATUS AcpiOsReadWritePciConfiguration(
        ACPI_PCI_ID *PciId,
        UINT32 Register,
        UINT64 *Value,
        UINT32 Width,
        bool Write) {

    // For the first call, probe the MCFG table and PIO space to attempt
    // to find a root complex. Since PCIe still populates the first 256
    // bytes of the PIO space, check for MCFG first, then PIO if we didn't
    // find anything of note.
    //
    // None of this is ideal, but it can be improved once we have a better
    // idea of the ACPI VM code's init process. For now the goal is simply
    // to provide the engine what it needs to complete its initialization.
    if (!acpi_pci_tbl.pci_probed) {
        if (acpi_probe_ecam() != MX_OK) {
            if (acpi_probe_legacy_pci() != MX_OK) {
                printf("ACPI: failed to find PCI/PCIe.\n");
            }
        }

        acpi_pci_tbl.pci_probed = true;
    }

    if (LOCAL_TRACE) {
        printf("ACPI %s PCI Config %x:%x:%x:%x register %#x width %u\n",
            Write ? "write" : "read" ,PciId->Segment, PciId->Bus, PciId->Device, PciId->Function, Register, Width);
    }

    // Only segment 0 is supported for now
    if (PciId->Segment != 0) {
        printf("ACPI: read/write config, segment != 0 not supported.\n");
        return AE_ERROR;
    }

    // Check bounds of device and function offsets
    if (PciId->Device >= PCIE_MAX_DEVICES_PER_BUS
            || PciId->Function >= PCIE_MAX_FUNCTIONS_PER_DEVICE) {
        return AE_ERROR;
    }

    // PCI config only supports up to 32 bit values
    if (Write && (*Value > UINT_MAX)) {
        printf("ACPI: read/write config, Value passed does not fit confg registers.\n");
    }

    // Clear higher bits before a read
    if (!Write) {
        *Value = 0;
    }

    ACPI_STATUS status = AE_ERROR;
    if (acpi_pci_tbl.ecam != NULL) {
        status = acpi_pci_ecam_cfg_rw(PciId, Register, Value, Width, Write);
    } else if (acpi_pci_tbl.has_legacy) {
        // TODO: ARM PIO?
#if __x86_64__
        // PIO config space doesn't have read/write cycles larger than 32 bits
        status = acpi_pci_x86_pio_cfg_rw(PciId, Register, (uint32_t*)Value, Width, Write);
#endif
    }

    return status;
}
/**
 * @brief Read a value from a PCI configuration register.
 *
 * @param PciId The full PCI configuration space address, consisting of a
 *        segment number, bus number, device number, and function number.
 * @param Register The PCI register address to be read from.
 * @param Value A pointer to a location where the data is to be returned.
 * @param Width The register width in bits, either 8, 16, 32, or 64.
 *
 * @return Exception code that indicates success or reason for failure.
 */
ACPI_STATUS AcpiOsReadPciConfiguration(
        ACPI_PCI_ID *PciId,
        UINT32 Register,
        UINT64 *Value,
        UINT32 Width) {

    return AcpiOsReadWritePciConfiguration(PciId, Register, Value, Width, false);
}

/**
 * @brief Write a value to a PCI configuration register.
 *
 * @param PciId The full PCI configuration space address, consisting of a
 *        segment number, bus number, device number, and function number.
 * @param Register The PCI register address to be written to.
 * @param Value Data to be written.
 * @param Width The register width in bits, either 8, 16, or 32.
 *
 * @return Exception code that indicates success or reason for failure.
 */
ACPI_STATUS AcpiOsWritePciConfiguration(
        ACPI_PCI_ID *PciId,
        UINT32 Register,
        UINT64 Value,
        UINT32 Width) {

    return AcpiOsReadWritePciConfiguration(PciId, Register, &Value, Width, true);
}

/**
 * @brief Formatted stream output.
 *
 * @param Format A standard print format string.
 * @param ...
 */
void ACPI_INTERNAL_VAR_XFACE AcpiOsPrintf(const char *Format, ...) {
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
void AcpiOsVprintf(const char *Format, va_list Args) {
}

/**
 * @brief Get current value of the system timer
 *
 * @return The current value of the system timer in 100-ns units.
 */
UINT64 AcpiOsGetTimer() {
    assert(false);
    return 0;
}

/**
 * @brief Break to the debugger or display a breakpoint message.
 *
 * @param Function Signal to be sent to the host operating system.  Either
 *        ACPI_SIGNAL_FATAL or ACPI_SIGNAL_BREAKPOINT
 * @param Info Data associated with the signal; type depends on signal type.
 *
 * @return Exception code that indicates success or reason for failure.
 */
ACPI_STATUS AcpiOsSignal(
        UINT32 Function,
        void *Info) {
    assert(false);
    return AE_OK;
}

/* @brief Acquire the ACPI global lock
 *
 * Implementation for ACPI_ACQUIRE_GLOBAL_LOCK
 *
 * @param FacsPtr pointer to the FACS ACPI structure
 *
 * @return True if the lock was successfully acquired
 */
bool _acpica_acquire_global_lock(void *FacsPtr)
{
    ACPI_TABLE_FACS *table = FacsPtr;
    uint32_t old_val, new_val, test_val;
    do {
        old_val = test_val = table->GlobalLock;
        new_val = old_val & ~ACPI_GLOCK_PENDING;
        // If the lock is owned, we'll mark it pending
        if (new_val & ACPI_GLOCK_OWNED) {
            new_val |= ACPI_GLOCK_PENDING;
        }
        new_val |= ACPI_GLOCK_OWNED;
        __atomic_compare_exchange_n(&table->GlobalLock, &old_val, new_val, false,
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
bool _acpica_release_global_lock(void *FacsPtr)
{
    ACPI_TABLE_FACS *table = FacsPtr;
    uint32_t old_val, new_val, test_val;
    do {
        old_val = test_val = table->GlobalLock;
        new_val = old_val & ~(ACPI_GLOCK_PENDING | ACPI_GLOCK_OWNED);
        __atomic_compare_exchange_n(&table->GlobalLock, &old_val, new_val, false,
                                    __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
    } while (old_val != test_val);

    return !!(old_val & ACPI_GLOCK_PENDING);
}
