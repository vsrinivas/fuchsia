// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <errno.h>
#include <lib/sync/mutex.h>
#include <lib/zircon-internal/thread_annotations.h>
#include <pthread.h>

#include <ctime>

#include "zircon/system/ulib/acpica/osfuchsia.h"

// The |acpi_spinlock_lock| is used to guarantee that all spinlock acquisitions will
// be uncontested in certain circumstances.  This allows us to ensure that
// the codepaths for entering an S-state will not need to wait for some other thread
// to finish processing.  The scheme works with the following protocol:
//
// Normal operational threads: If attempting to acquire a lock, and the thread
// holds no spinlock yet, then acquire |acpi_spinlock_lock| in READ mode before
// acquiring the desired lock.  For all other lock acquisitions behave normally.
// If a thread is releasing its last held lock, release the |acpi_spinlock_lock|.
//
// Non-contested thread: To enter non-contested mode, call
// |acpica_enable_noncontested_mode| while not holding any ACPI spinlock.  This will
// acquire the |acpi_spinlock_lock| in WRITE mode.  Call
// |acpica_disable_noncontested_mode| while not holding any ACPI spinlock to release
// the |acpi_spinlock_lock|.
//
// Non-contested mode needs to apply to both spin locks and mutexes to prevent deadlock.
// TODO(fxbug.dev/79085): remove this, and replace it with a higher-level lock on the ACPI FIDL
// protocol. This is risky because pthread timeouts use CLOCK_REALTIME, which makes no forward
// progress in early boot.
static pthread_rwlock_t acpi_spinlock_lock = PTHREAD_RWLOCK_INITIALIZER;
static thread_local uint64_t acpi_spinlocks_held = 0;

void acpica_enable_noncontested_mode() {
  ZX_ASSERT(acpi_spinlocks_held == 0);
  int ret = pthread_rwlock_wrlock(&acpi_spinlock_lock);
  ZX_ASSERT(ret == 0);
  acpi_spinlocks_held++;
}

void acpica_disable_noncontested_mode() {
  ZX_ASSERT(acpi_spinlocks_held == 1);
  int ret = pthread_rwlock_unlock(&acpi_spinlock_lock);
  ZX_ASSERT(ret == 0);
  acpi_spinlocks_held--;
}

static std::timespec timeout_to_timespec(UINT16 Timeout) {
  std::timespec ts{.tv_sec = 0, .tv_nsec = 0};
  ZX_ASSERT(std::timespec_get(&ts, TIME_UTC) != 0);
  return zx_timespec_from_duration(
      zx_duration_add_duration(zx_duration_from_timespec(ts), ZX_MSEC(Timeout)));
}

/**
 * @brief Create a mutex.
 *
 * @param OutHandle A pointer to a locaton where a handle to the mutex is
 *        to be returned.
 *
 * @return AE_OK The mutex was successfully created.
 * @return AE_BAD_PARAMETER The OutHandle pointer is NULL.
 * @return AE_NO_MEMORY Insufficient memory to create the mutex.
 */
ACPI_STATUS AcpiOsCreateMutex(ACPI_MUTEX* OutHandle) {
  sync_mutex_t* lock = new sync_mutex_t();
  if (!lock) {
    return AE_NO_MEMORY;
  }

  *OutHandle = lock;
  return AE_OK;
}

/**
 * @brief Delete a mutex.
 *
 * @param Handle A handle to a mutex objected that was returned by a
 *        previous call to AcpiOsCreateMutex.
 */
void AcpiOsDeleteMutex(ACPI_MUTEX Handle) { delete Handle; }

/**
 * @brief Acquire a mutex.
 *
 * @param Handle A handle to a mutex objected that was returned by a
 *        previous call to AcpiOsCreateMutex.
 * @param Timeout How long the caller is willing to wait for the requested
 *        units, in milliseconds.  A value of -1 indicates that the caller
 *        is willing to wait forever. Timeout may be 0.
 *
 * @return AE_OK The requested units were successfully received.
 * @return AE_BAD_PARAMETER The Handle is invalid.
 * @return AE_TIME The mutex could not be acquired within the specified time.
 */
ACPI_STATUS AcpiOsAcquireMutex(ACPI_MUTEX Handle, UINT16 Timeout)
    TA_TRY_ACQ(AE_OK, Handle) TA_NO_THREAD_SAFETY_ANALYSIS {
  if (Timeout == UINT16_MAX) {
    if (acpi_spinlocks_held == 0) {
      int ret = pthread_rwlock_rdlock(&acpi_spinlock_lock);
      ZX_ASSERT(ret == 0);
    }

    sync_mutex_lock(Handle);
  } else {
    zx_time_t deadline = zx_deadline_after(ZX_MSEC(Timeout));

    if (acpi_spinlocks_held == 0) {
      int ret;
      if (Timeout == 0) {
        // We don't want to use pthread_rwlock_timedrdlock here, because it relies on
        // CLOCK_REALTIME. During early boot, CLOCK_REALTIME doesn't move forward.
        ret = pthread_rwlock_tryrdlock(&acpi_spinlock_lock);
        if (ret != 0) {
          return AE_TIME;
        }
      } else {
        // This relise on CLOCK_REALTIME. If the clock hasn't started, we will wait
        // indefinitely. There's not much else we can do.
        // TODO(fxbug.dev/79085): remove the rwlock from here.
        std::timespec then = timeout_to_timespec(Timeout);
        ret = pthread_rwlock_timedrdlock(&acpi_spinlock_lock, &then);
        if (ret == ETIMEDOUT)
          return AE_TIME;
      }
      ZX_ASSERT(ret == 0);
    }

    zx_status_t res = sync_mutex_timedlock(Handle, deadline);
    if (res == ZX_ERR_TIMED_OUT) {
      if (acpi_spinlocks_held == 0) {
        int res = pthread_rwlock_unlock(&acpi_spinlock_lock);
        ZX_ASSERT(res == 0);
      }
      return AE_TIME;
    }
    ZX_ASSERT(res == ZX_OK);
  }

  acpi_spinlocks_held++;
  return AE_OK;
}

/**
 * @brief Release a mutex.
 *
 * @param Handle A handle to a mutex objected that was returned by a
 *        previous call to AcpiOsCreateMutex.
 */
void AcpiOsReleaseMutex(ACPI_MUTEX Handle) TA_REL(Handle) {
  sync_mutex_unlock(Handle);

  acpi_spinlocks_held--;
  if (acpi_spinlocks_held == 0) {
    int ret = pthread_rwlock_unlock(&acpi_spinlock_lock);
    ZX_ASSERT(ret == 0);
  }
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
ACPI_STATUS AcpiOsCreateLock(ACPI_SPINLOCK* OutHandle) {
  // Since we don't have a notion of interrupt context in usermode, just make
  // these mutexes.
  return AcpiOsCreateMutex(OutHandle);
}

/**
 * @brief Delete a spin lock.
 *
 * @param Handle A handle to a lock objected that was returned by a
 *        previous call to AcpiOsCreateLock.
 */
void AcpiOsDeleteLock(ACPI_SPINLOCK Handle) { AcpiOsDeleteMutex(Handle); }

/**
 * @brief Acquire a spin lock.
 *
 * @param Handle A handle to a lock objected that was returned by a
 *        previous call to AcpiOsCreateLock.
 *
 * @return Platform-dependent CPU flags.  To be used when the lock is released.
 */
ACPI_CPU_FLAGS AcpiOsAcquireLock(ACPI_SPINLOCK Handle) TA_ACQ(Handle) TA_NO_THREAD_SAFETY_ANALYSIS {
  int ret = AcpiOsAcquireMutex(Handle, UINT16_MAX);
  // The thread safety analysis doesn't seem to handle the noreturn inside of the assert
  ZX_ASSERT(ret == AE_OK);
  return 0;
}

/**
 * @brief Release a spin lock.
 *
 * @param Handle A handle to a lock objected that was returned by a
 *        previous call to AcpiOsCreateLock.
 * @param Flags CPU Flags that were returned from AcpiOsAcquireLock.
 */
void AcpiOsReleaseLock(ACPI_SPINLOCK Handle, ACPI_CPU_FLAGS Flags) TA_REL(Handle) {
  AcpiOsReleaseMutex(Handle);
}
