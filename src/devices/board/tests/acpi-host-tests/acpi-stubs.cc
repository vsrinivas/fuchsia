// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// ACPI methods for running ACPICA in a host environment.

#include <pthread.h>
#include <semaphore.h>
#include <stdio.h>
#include <sys/time.h>

#include <chrono>
#include <cstdlib>
#include <thread>

#include <acpica/acpi.h>

#include "src/devices/board/tests/acpi-host-tests/table-manager.h"
#include "third_party/acpica/source/include/acexcep.h"
#include "third_party/acpica/source/include/acpiosxf.h"
#include "third_party/acpica/source/include/actypes.h"

extern "C" {
#include <unistd.h>

// Environment and ACPI tables
ACPI_STATUS AcpiOsInitialize() { return AE_OK; }
ACPI_STATUS AcpiOsTerminate() { return AE_OK; }

ACPI_PHYSICAL_ADDRESS AcpiOsGetRootPointer() {
  return reinterpret_cast<ACPI_PHYSICAL_ADDRESS>(acpi::AcpiTableManager::Get()->GetRsdp());
}

ACPI_STATUS AcpiOsPredefinedOverride(const ACPI_PREDEFINED_NAMES* InitVal, ACPI_STRING* NewVal) {
  *NewVal = nullptr;
  return AE_OK;
}

ACPI_STATUS AcpiOsTableOverride(ACPI_TABLE_HEADER* ExistingTable, ACPI_TABLE_HEADER** NewTable) {
  *NewTable = nullptr;
  return AE_OK;
}

ACPI_STATUS AcpiOsPhysicalTableOverride(ACPI_TABLE_HEADER* ExistingTable,
                                        ACPI_PHYSICAL_ADDRESS* NewAddress, UINT32* NewTableLength) {
  *NewAddress = 0;
  *NewTableLength = 0;
  return AE_OK;
}

// Memory Management
void* AcpiOsMapMemory(ACPI_PHYSICAL_ADDRESS Where, ACPI_SIZE Length) {
  return reinterpret_cast<void*>(Where);
}

void AcpiOsUnmapMemory(void* LogicalAddress, ACPI_SIZE Size) {}

ACPI_STATUS AcpiOsGetPhysicalAddress(void* LogicalAddress, ACPI_PHYSICAL_ADDRESS* PhysicalAddress) {
  return AE_NOT_IMPLEMENTED;
}

void* AcpiOsAllocate(ACPI_SIZE size) { return malloc(size); }
void AcpiOsFree(void* ptr) { free(ptr); }

// Multithreading and Scheduling Services
ACPI_STATUS AcpiOsExecute(ACPI_EXECUTE_TYPE Type, ACPI_OSD_EXEC_CALLBACK Function, void* Context) {
  // ACPICA says this should be asynchronous, but it's probably fine.
  Function(Context);
  return AE_OK;
}

ACPI_THREAD_ID AcpiOsGetThreadId() { return pthread_self(); }

void AcpiOsSleep(UINT64 Milliseconds) {
  std::this_thread::sleep_for(std::chrono::milliseconds(Milliseconds));
}
void AcpiOsStall(UINT32 Microseconds) {
  std::this_thread::sleep_for(std::chrono::microseconds(Microseconds));
}

void AcpiOsWaitEventsComplete() {}

// Mutual Exclusion and Synchronisation
ACPI_STATUS AcpiOsCreateSemaphore(UINT32 MaxUnits, UINT32 InitialUnits, void** OutHandle) {
  sem_t* sem = static_cast<sem_t*>(AcpiOsAllocate(sizeof(*sem)));
  if (sem_init(sem, 0, InitialUnits) < 0) {
    free(sem);
    return AE_BAD_PARAMETER;
  }
  *OutHandle = sem;
  return AE_OK;
}

ACPI_STATUS AcpiOsDeleteSemaphore(void* Handle) {
  free(Handle);
  return AE_OK;
}

ACPI_STATUS AcpiOsWaitSemaphore(void* Handle, UINT32 Units, UINT16 Timeout) {
  sem_t* sem = static_cast<sem_t*>(Handle);
  // We either don't wait or wait forever. Nothing seems to use anything else.
  if (Timeout == 0) {
    if (sem_trywait(sem) == -1) {
      return AE_TIME;
    }
  } else {
    if (sem_wait(sem) == -1) {
      return AE_TIME;
    }
  }

  return AE_OK;
}

ACPI_STATUS AcpiOsSignalSemaphore(void* Handle, UINT32 Units) {
  sem_t* sem = static_cast<sem_t*>(Handle);
  if (sem_post(sem) == -1) {
    return AE_LIMIT;
  }
  return AE_OK;
}

ACPI_STATUS
AcpiOsCreateLock(ACPI_SPINLOCK* OutHandle) { return (AcpiOsCreateSemaphore(1, 1, OutHandle)); }

void AcpiOsDeleteLock(ACPI_SPINLOCK Handle) { AcpiOsDeleteSemaphore(Handle); }

ACPI_CPU_FLAGS
AcpiOsAcquireLock(ACPI_HANDLE Handle) {
  AcpiOsWaitSemaphore(Handle, 1, 0xFFFF);
  return (0);
}

void AcpiOsReleaseLock(ACPI_SPINLOCK Handle, ACPI_CPU_FLAGS Flags) {
  AcpiOsSignalSemaphore(Handle, 1);
}

// Interrupt Handling

ACPI_STATUS AcpiOsInstallInterruptHandler(UINT32 InterruptNumber, ACPI_OSD_HANDLER ServiceRoutine,
                                          void* Context) {
  return AE_NOT_IMPLEMENTED;
}

ACPI_STATUS AcpiOsRemoveInterruptHandler(UINT32 InterruptNumber, ACPI_OSD_HANDLER ServiceRoutine) {
  return AE_NOT_IMPLEMENTED;
}

// Memory Access and Memory Mapping
ACPI_STATUS AcpiOsReadMemory(ACPI_PHYSICAL_ADDRESS Address, UINT64* Value, UINT32 Width) {
  *Value = 0;
  return AE_OK;
}

ACPI_STATUS AcpiOsWriteMemory(ACPI_PHYSICAL_ADDRESS Address, UINT64 Value, UINT32 Width) {
  return AE_OK;
}

// Port Input/Output
ACPI_STATUS AcpiOsReadPort(ACPI_IO_ADDRESS Address, UINT32* Value, UINT32 Width) {
  *Value = 0;
  return AE_OK;
}

ACPI_STATUS AcpiOsWritePort(ACPI_IO_ADDRESS Address, UINT32 Value, UINT32 Width) { return AE_OK; }
}

// PCI Configuration Space Access
ACPI_STATUS AcpiOsReadPciConfiguration(ACPI_PCI_ID* PciId, UINT32 Reg, UINT64* Value,
                                       UINT32 Width) {
  *Value = 0;
  return AE_OK;
}

ACPI_STATUS AcpiOsWritePciConfiguration(ACPI_PCI_ID* PciId, UINT32 Reg, UINT64 Value,
                                        UINT32 Width) {
  return AE_OK;
}

void AcpiOsPrintf(const char* Format, ...) {
  va_list ap;
  va_start(ap, Format);
  vprintf(Format, ap);
  va_end(ap);
}

void AcpiOsVprintf(const char* Format, va_list Args) { vprintf(Format, Args); }

void AcpiOsRedirectOutput(void* Destination) {}

// Miscellaneous

UINT64 AcpiOsGetTimer() {
  struct timeval time;
  gettimeofday(&time, NULL);

  return (((UINT64)time.tv_sec * ACPI_100NSEC_PER_SEC) +
          ((UINT64)time.tv_usec * ACPI_100NSEC_PER_USEC));
}

ACPI_STATUS AcpiOsSignal(UINT32 Function, void* Info) { return AE_OK; }
