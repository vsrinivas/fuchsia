// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <assert.h>
#include <debug.h>
#include <err.h>
#include <stdio.h>
#include <trace.h>
#include <arch/ops.h>
#include <dev/interrupt.h>
#include <kernel/cond.h>
#include <kernel/mutex.h>
#include <kernel/semaphore.h>
#include <kernel/spinlock.h>
#include <kernel/thread.h>
#include <kernel/vm.h>
#include <arch/x86/apic.h>
#include <arch/x86/interrupts.h>

#if !ARCH_X86
#error "Unsupported architecture"
#endif
// Needed for port IO
#include <arch/x86.h>

#include "acpi.h"

#define _COMPONENT          ACPI_OS_SERVICES
ACPI_MODULE_NAME    ("osmagenta")

#define LOCAL_TRACE 0

/**
 * @brief Initialize the OSL subsystem.
 *
 * This function allows the OSL to initialize itself.  It is called during
 * intiialization of the ACPICA subsystem.
 *
 * @return Initialization status
 */
ACPI_STATUS AcpiOsInitialize() {
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
    return AE_OK;
}

extern uint32_t bootloader_acpi_rsdp;
/**
 * @brief Obtain the Root ACPI table pointer (RSDP).
 *
 * @return The physical address of the RSDP
 */
ACPI_PHYSICAL_ADDRESS AcpiOsGetRootPointer() {
    if (bootloader_acpi_rsdp) {
        return bootloader_acpi_rsdp;
    } else {
        ACPI_PHYSICAL_ADDRESS TableAddress = 0;
        ACPI_STATUS status = AcpiFindRootPointer(&TableAddress);

        if (status != AE_OK) {
            return 0;
        }
        return TableAddress;
    }
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

    ACPI_PHYSICAL_ADDRESS aligned_address = ROUNDDOWN(PhysicalAddress, PAGE_SIZE);
    ACPI_PHYSICAL_ADDRESS end = ROUNDUP(PhysicalAddress + Length, PAGE_SIZE);

    vmm_aspace_t *kernel_aspace = vmm_get_kernel_aspace();
    void *vaddr = NULL;
    status_t status = vmm_alloc_physical(
            kernel_aspace,
            "acpi_mapping",
            end - aligned_address,
            &vaddr,
            PAGE_SIZE_SHIFT,
            0,
            aligned_address,
            0,
            ARCH_MMU_FLAG_PERM_READ | ARCH_MMU_FLAG_PERM_WRITE);
    if (status != NO_ERROR) {
        return NULL;
    }
    return vaddr + (PhysicalAddress - aligned_address);
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
    vmm_aspace_t *kernel_aspace = vmm_get_kernel_aspace();
    status_t status = vmm_free_region(kernel_aspace, (vaddr_t)LogicalAddress);
    if (status != NO_ERROR) {
        TRACEF("WARNING: ACPI failed to free region %p, size %" PRIu64 "\n",
               LogicalAddress, (uint64_t)Length);
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
static_assert(sizeof(ACPI_THREAD_ID) >= sizeof(uintptr_t), "");
ACPI_THREAD_ID AcpiOsGetThreadId() {
    // Just use the address of the thread_t
    return (uintptr_t)get_current_thread();
}

/* Data and structures used for implementing AcpiOsExecute and
 * AcpiOsWaitEventsComplete */
static mutex_t os_execute_lock = MUTEX_INITIAL_VALUE(os_execute_lock);
static cond_t os_execute_cond = COND_INITIAL_VALUE(os_execute_cond);
static int os_execute_tasks = 0;

struct acpi_os_task_ctx {
    ACPI_OSD_EXEC_CALLBACK func;
    void *ctx;
};

static int acpi_os_task(void *raw_ctx) {
    struct acpi_os_task_ctx *ctx = raw_ctx;

    ctx->func(ctx->ctx);

    mutex_acquire(&os_execute_lock);
    os_execute_tasks--;
    cond_broadcast(&os_execute_cond);
    mutex_release(&os_execute_lock);

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

    mutex_acquire(&os_execute_lock);
    os_execute_tasks++;
    mutex_release(&os_execute_lock);

    thread_t *t = thread_create(
            "acpi_os_exec",
            acpi_os_task, ctx,
            DEFAULT_PRIORITY, DEFAULT_STACK_SIZE);
    if (!t) {
        free(ctx);
        mutex_acquire(&os_execute_lock);
        os_execute_tasks--;
        cond_broadcast(&os_execute_cond);
        mutex_release(&os_execute_lock);
        return AE_NO_MEMORY;
    }
    status_t status = thread_detach_and_resume(t);
    if (status != NO_ERROR) {
        thread_resume(t);
    }

    return AE_OK;
}

/**
 * @brief Wait for completion of asynchronous events.
 *
 * This function blocks until all asynchronous events initiated by
 * AcpiOsExecute have completed.
 */
void AcpiOsWaitEventsComplete(void) {
    mutex_acquire(&os_execute_lock);
    while (os_execute_tasks > 0) {
        cond_wait_timeout(&os_execute_cond, &os_execute_lock, INFINITE_TIME);
    }
    mutex_release(&os_execute_lock);
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
    thread_sleep(Milliseconds);
}

/**
 * @brief Wait for a short amount of time (fine granularity).
 *
 * Execution of the running thread is not suspended for this time.
 *
 * @param Microseconds The amount of time to delay, in microseconds.
 */
void AcpiOsStall(UINT32 Microseconds) {
    spin(Microseconds);
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
    semaphore_t *sem = malloc(sizeof(semaphore_t));
    if (!sem) {
        return AE_NO_MEMORY;
    }
    sem_init(sem, InitialUnits);
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
    // TODO: Implement support for Units > 1
    ASSERT(Units == 1);
    lk_time_t timeout = Time;
    if (Time == 0xffff) {
        timeout = INFINITE_TIME;
    }
    status_t status = sem_timedwait(Handle, timeout);
    if (status == ERR_TIMED_OUT) {
        return AE_TIME;
    }
    // The API doesn't have any bailout other than timeout, so if this was
    // unsuccessfuly for some other reason, we can't really do anything...
    ASSERT(status == NO_ERROR);
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
    ASSERT(Units == 1);

    sem_post(Handle, false /* don't reschedule (TODO: revisit?) */);
    return AE_OK;
}

/**
 * @brief Create a spin lock.
 *
 * @param OutHandle A pointer to a locaton where a handle to the lockis
 *        to be returned.
 *
 * @return AE_OK The semaphore was successfully created.
 * @return AE_BAD_PARAMETER The OutHandle pointer is NULL.
 * @return AE_NO_MEMORY Insufficient memory to create the lock.
 */
ACPI_STATUS AcpiOsCreateLock(ACPI_SPINLOCK *OutHandle) {
    spin_lock_t *lock = malloc(sizeof(spin_lock_t));
    if (!lock) {
        return AE_NO_MEMORY;
    }
    spin_lock_init(lock);
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
    spin_lock_saved_state_t state;
    spin_lock_irqsave(Handle, state);
    return state;
}

/**
 * @brief Release a spin lock.
 *
 * @param Handle A handle to a lock objected that was returned by a
 *        previous call to AcpiOsCreateLock.
 * @param Flags CPU Flags that were returned from AcpiOsAcquireLock.
 */
void AcpiOsReleaseLock(ACPI_SPINLOCK Handle, ACPI_CPU_FLAGS Flags) {
    spin_unlock_irqrestore(Handle, Flags);
}

// Wrapper structs for interfacing between our interrupt handler convention and
// ACPICA's
struct acpi_irq_wrapper_arg {
    ACPI_OSD_HANDLER handler;
    void *context;
};
enum handler_return acpi_irq_wrapper(void *arg);
enum handler_return acpi_irq_wrapper(void *arg) {
    struct acpi_irq_wrapper_arg *real_arg = (struct acpi_irq_wrapper_arg *)arg;
    real_arg->handler(real_arg->context);
    // TODO: Should we do something with the return value from the handler?
    return INT_NO_RESCHEDULE;
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

    ASSERT(InterruptLevel == 0x9); // SCI
    apic_io_configure_isa_irq(
        InterruptLevel,
        DELIVERY_MODE_FIXED,
        IO_APIC_IRQ_MASK,
        DST_MODE_PHYSICAL,
        apic_local_id(),
        0);

    struct acpi_irq_wrapper_arg *arg = malloc(sizeof(*arg));
    if (!arg) {
        return AE_NO_MEMORY;
    }
    arg->handler = Handler;
    arg->context = Context;
    register_int_handler(InterruptLevel, acpi_irq_wrapper, arg);
    unmask_interrupt(InterruptLevel);
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
    PANIC_UNIMPLEMENTED;
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
    PANIC_UNIMPLEMENTED;
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
    PANIC_UNIMPLEMENTED;
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
    LTRACEF("Reading PCI ID ptr %p\n", PciId);
    LTRACEF("Reading PCI: %02x:%02x.%x, reg %#08x, width %u\n", PciId->Bus, PciId->Device, PciId->Function, Register, Width);
    // TODO: Maybe implement for real
    // Pretending the answer is 0 for now makes our hardware targets work fine.
    // On primary target it attempts to read some registers on the LPC endpoint.
    *Value = 0;
    return AE_OK;
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
    // TODO: Maybe implement
    return AE_ERROR;
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
    PANIC_UNIMPLEMENTED;
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
    PANIC_UNIMPLEMENTED;
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
