// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/sync/condition.h>
#include <lib/sync/mutex.h>

#include <acpica/acpi.h>

// Semaphore implementation using condvar + mutex.
struct AcpiSemaphore {
 public:
  explicit AcpiSemaphore(uint32_t initial_count) : count_(initial_count) {}

  void Wait(uint32_t units) {
    sync_mutex_lock(&mutex_);
    while (count_ < units) {
      sync_condition_wait(&condition_, &mutex_);
    }
    sync_mutex_unlock(&mutex_);
  }

  ACPI_STATUS WaitWithDeadline(uint32_t units, zx_time_t deadline) {
    zx_status_t result = sync_mutex_timedlock(&mutex_, deadline);
    if (result == ZX_ERR_TIMED_OUT) {
      return AE_TIME;
    }
    sync_mutex_assert_held(&mutex_);
    while (result != ZX_ERR_TIMED_OUT && count_ < units && zx_clock_get_monotonic() < deadline) {
      result = sync_condition_timedwait(&condition_, &mutex_, deadline);
    }
    if (result == ZX_ERR_TIMED_OUT) {
      sync_mutex_unlock(&mutex_);
      return AE_TIME;
    }
    count_ -= units;
    sync_mutex_unlock(&mutex_);
    return AE_OK;
  }

  void Signal(uint32_t units) {
    sync_mutex_lock(&mutex_);
    count_ += units;
    if (units == 1) {
      sync_condition_signal(&condition_);
    } else {
      sync_condition_broadcast(&condition_);
    }
    sync_mutex_unlock(&mutex_);
  }

 private:
  sync_condition_t condition_;
  sync_mutex_t mutex_;
  uint32_t count_ __TA_GUARDED(mutex_);
};

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
ACPI_STATUS AcpiOsCreateSemaphore(UINT32 MaxUnits, UINT32 InitialUnits, ACPI_SEMAPHORE* OutHandle) {
  AcpiSemaphore* sem = new AcpiSemaphore(InitialUnits);
  if (!sem) {
    return AE_NO_MEMORY;
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
 */
ACPI_STATUS AcpiOsDeleteSemaphore(ACPI_SEMAPHORE Handle) {
  delete Handle;
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
ACPI_STATUS AcpiOsWaitSemaphore(ACPI_SEMAPHORE Handle, UINT32 Units, UINT16 Timeout) {
  if (Timeout == UINT16_MAX) {
    Handle->Wait(Units);
    return AE_OK;
  }

  zx_time_t deadline = zx_deadline_after(ZX_MSEC(Timeout));
  return Handle->WaitWithDeadline(Units, deadline);
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
ACPI_STATUS AcpiOsSignalSemaphore(ACPI_SEMAPHORE Handle, UINT32 Units) {
  Handle->Signal(Units);
  return AE_OK;
}
