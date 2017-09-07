// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <assert.h>
#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <threads.h>

#include <hw/inout.h>
#include <magenta/assert.h>
#include <magenta/process.h>
#include <magenta/syscalls.h>
#include <mxcpp/new.h>
#include <fbl/auto_lock.h>
#include <fbl/intrusive_hash_table.h>
#include <fbl/unique_ptr.h>

#if !defined(__x86_64__) && !defined(__x86__)
#error "Unsupported architecture"
#endif

#include "acpi.h"

__WEAK mx_handle_t root_resource_handle;

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

class AcpiOsMappingNode :
      public fbl::SinglyLinkedListable<fbl::unique_ptr<AcpiOsMappingNode>> {
public:
    using HashTable =
        fbl::HashTable<uintptr_t, fbl::unique_ptr<AcpiOsMappingNode>>;

    // @param vaddr Virtual address returned to ACPI, used as key to the hashtable.
    // @param vaddr_actual Actual virtual address of the mapping. May be different than
    //                     vaddr if it is unaligned.
    // @param length Length of the mapping
    // @param vmo_handle Handle to the mapped VMO
    AcpiOsMappingNode(uintptr_t vaddr, uintptr_t vaddr_actual,
                      size_t length, mx_handle_t vmo_handle);
    ~AcpiOsMappingNode();

    // Trait implementation for fbl::HashTable
    uintptr_t GetKey() const { return vaddr_; }
    static size_t GetHash(uintptr_t key) { return key; }

private:
    uintptr_t vaddr_;
    uintptr_t vaddr_actual_;
    size_t length_;
    mx_handle_t vmo_handle_;
};

fbl::Mutex os_mapping_lock;

AcpiOsMappingNode::HashTable os_mapping_tbl;

const size_t PCIE_MAX_DEVICES_PER_BUS = 32;
const size_t PCIE_MAX_FUNCTIONS_PER_DEVICE = 8;

AcpiOsMappingNode::AcpiOsMappingNode(uintptr_t vaddr, uintptr_t vaddr_actual,
                                     size_t length, mx_handle_t vmo_handle)
    : vaddr_(vaddr), vaddr_actual_(vaddr_actual),
      length_(length), vmo_handle_(vmo_handle) {
}

AcpiOsMappingNode::~AcpiOsMappingNode() {
    mx_vmar_unmap(mx_vmar_root_self(), (uintptr_t)vaddr_actual_, length_);
    mx_handle_close(vmo_handle_);
}

static mx_status_t mmap_physical(mx_paddr_t phys, size_t size, uint32_t cache_policy,
                                 mx_handle_t* out_vmo, mx_vaddr_t* out_vaddr) {
    mx_handle_t vmo;
    mx_vaddr_t vaddr;
    mx_status_t st = mx_vmo_create_physical(root_resource_handle, phys, size, &vmo);
    if (st != MX_OK) {
        return st;
    }
    st = mx_vmo_set_cache_policy(vmo, cache_policy);
    if (st != MX_OK) {
        mx_handle_close(vmo);
        return st;
    }
    st = mx_vmar_map(mx_vmar_root_self(), 0, vmo, 0, size,
                     MX_VM_FLAG_PERM_READ | MX_VM_FLAG_PERM_WRITE | MX_VM_FLAG_MAP_RANGE,
                     &vaddr);
    if (st != MX_OK) {
        mx_handle_close(vmo);
        return st;
    } else {
        *out_vmo = vmo;
        *out_vaddr = vaddr;
        return MX_OK;
    }
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

    uint32_t uefi_rsdp = (uint32_t)mx_acpi_uefi_rsdp(root_resource_handle);
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

    fbl::AutoLock lock(&os_mapping_lock);

    // Caution: PhysicalAddress might not be page-aligned, Length might not
    // be a page multiple.

    ACPI_PHYSICAL_ADDRESS aligned_address = PhysicalAddress & ~(PAGE_SIZE - 1);
    ACPI_PHYSICAL_ADDRESS end = (PhysicalAddress + Length + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);

    uintptr_t vaddr;
    size_t length = end - aligned_address;
    mx_handle_t vmo;
    mx_status_t status = mmap_physical(aligned_address, end - aligned_address,
                                       MX_CACHE_POLICY_CACHED, &vmo, &vaddr);
    if (status != MX_OK) {
        return NULL;
    }

    void* out_addr = (void*)(vaddr + (PhysicalAddress - aligned_address));
    fbl::unique_ptr<AcpiOsMappingNode> mn(
            new AcpiOsMappingNode(reinterpret_cast<uintptr_t>(out_addr),
                                  vaddr, length, vmo));
    os_mapping_tbl.insert(fbl::move(mn));

    return out_addr;
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
    fbl::AutoLock lock(&os_mapping_lock);
    fbl::unique_ptr<AcpiOsMappingNode> mn = os_mapping_tbl.erase((uintptr_t)LogicalAddress);
    if (mn == NULL) {
        printf("AcpiOsUnmapMemory nonexisting mapping %p\n", LogicalAddress);
    }
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
    struct acpi_os_task_ctx *ctx = (struct acpi_os_task_ctx*)raw_ctx;

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

    struct acpi_os_task_ctx *ctx = (struct acpi_os_task_ctx*)malloc(sizeof(*ctx));
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
    sem_t *sem = (sem_t*)malloc(sizeof(sem_t));
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
        UINT16 Timeout) {

    if (Timeout == UINT16_MAX) {
        if (sem_wait(Handle) < 0) {
            MX_ASSERT_MSG(false, "sem_wait failed %d", errno);
        }
        return AE_OK;
    }

    mx_time_t now = mx_time_get(MX_CLOCK_UTC);
    struct timespec then = {
        .tv_sec = static_cast<time_t>(now / MX_SEC(1)),
        .tv_nsec = static_cast<long>(now % MX_SEC(1)),
    };
    then.tv_nsec += MX_MSEC(Timeout);
    if (then.tv_nsec > static_cast<long>(MX_SEC(1))) {
        then.tv_sec += then.tv_nsec / MX_SEC(1);
        then.tv_nsec %= MX_SEC(1);
    }

    if (sem_timedwait(Handle, &then) < 0) {
        MX_ASSERT_MSG(errno == ETIMEDOUT, "sem_timedwait failed unexpectedly %d", errno);
        return AE_TIME;
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
    mtx_t* lock = (mtx_t*)malloc(sizeof(mtx_t));
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

    struct acpi_irq_thread_arg *arg = (struct acpi_irq_thread_arg*)malloc(sizeof(*arg));
    if (!arg) {
        return AE_NO_MEMORY;
    }

    mx_handle_t handle;
    mx_status_t status = mx_interrupt_create(root_resource_handle, InterruptLevel,
                                             MX_FLAG_REMAP_IRQ, &handle);
    if (status != MX_OK) {
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
            *Value = inp((uint16_t)Address);
            break;
        case 16:
            *Value = inpw((uint16_t)Address);
            break;
        case 32:
            *Value = inpd((uint16_t)Address);
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
            outp((uint16_t)Address, (uint8_t)Value);
            break;
        case 16:
            outpw((uint16_t)Address, (uint16_t)Value);
            break;
        case 32:
            outpd((uint16_t)Address, (uint32_t)Value);
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

    if (LOCAL_TRACE) {
        printf("ACPIOS: %s PCI Config %x:%x:%x:%x register %#x width %u\n",
            Write ? "write" : "read" ,PciId->Segment, PciId->Bus, PciId->Device, PciId->Function, Register, Width);
    }

    // Only segment 0 is supported for now
    if (PciId->Segment != 0) {
        printf("ACPIOS: read/write config, segment != 0 not supported.\n");
        return AE_ERROR;
    }

    // Check bounds of device and function offsets
    if (PciId->Device >= PCIE_MAX_DEVICES_PER_BUS
            || PciId->Function >= PCIE_MAX_FUNCTIONS_PER_DEVICE) {
        printf("ACPIOS: device out of reasonable bounds.\n");
        return AE_ERROR;
    }

    // PCI config only supports up to 32 bit values
    if (Write && (*Value > UINT_MAX)) {
        printf("ACPIOS: read/write config, Value passed does not fit confg registers.\n");
    }

    // Clear higher bits before a read
    if (!Write) {
        *Value = 0;
    }

#if __x86_64__
    uint8_t bus = static_cast<uint8_t>(PciId->Bus);
    uint8_t dev = static_cast<uint8_t>(PciId->Device);
    uint8_t func = static_cast<uint8_t>(PciId->Function);
    uint8_t offset = static_cast<uint8_t>(Register);
    uint8_t width = static_cast<uint8_t>(Width);
    uint32_t val = *Value & 0xFFFFFFFF; // PIO access can only be 32 bits
    mx_status_t status = mx_pci_cfg_pio_rw(root_resource_handle, bus, dev, func, offset,
                                           &val, width, Write);

    *Value = val;

#ifdef ACPI_DEBUG_OUTPUT
    if (status != MX_OK) {
        printf("ACPIOS: pci rw error: %d\n", status);
    }
#endif // ACPI_DEBUG_OUTPUT
    return (status == MX_OK) ? AE_OK : AE_ERROR;
#endif // __x86_64__

    return AE_NOT_IMPLEMENTED;
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
    ACPI_TABLE_FACS *table = (ACPI_TABLE_FACS*)FacsPtr;
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
    ACPI_TABLE_FACS *table = (ACPI_TABLE_FACS*)FacsPtr;
    uint32_t old_val, new_val, test_val;
    do {
        old_val = test_val = table->GlobalLock;
        new_val = old_val & ~(ACPI_GLOCK_PENDING | ACPI_GLOCK_OWNED);
        __atomic_compare_exchange_n(&table->GlobalLock, &old_val, new_val, false,
                                    __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
    } while (old_val != test_val);

    return !!(old_val & ACPI_GLOCK_PENDING);
}
