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
#include <vm/vm_aspace.h>

#if ARCH_X86
#include <platform/pc/bootloader.h>
#else
#error "Unsupported architecture"
#endif

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

/**
 * @brief Obtain the Root ACPI table pointer (RSDP).
 *
 * @return The physical address of the RSDP
 */
ACPI_PHYSICAL_ADDRESS AcpiOsGetRootPointer() {
    if (bootloader.acpi_rsdp) {
        return bootloader.acpi_rsdp;
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

    void *vaddr = NULL;
    status_t status = VmAspace::kernel_aspace()->AllocPhysical(
            "acpi_mapping",
            end - aligned_address,
            &vaddr,
            PAGE_SIZE_SHIFT,
            aligned_address,
            0,
            ARCH_MMU_FLAG_PERM_READ | ARCH_MMU_FLAG_PERM_WRITE);
    if (status != MX_OK) {
        return NULL;
    }
    const uintptr_t real_addr =
            reinterpret_cast<uintptr_t>(vaddr) + (PhysicalAddress - aligned_address);
    return reinterpret_cast<void*>(real_addr);
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
    status_t status = VmAspace::kernel_aspace()->FreeRegion(
            reinterpret_cast<vaddr_t>(LogicalAddress));
    if (status != MX_OK) {
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
