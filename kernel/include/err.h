// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2008-2014 Travis Geiselbrecht
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef __ERR_H
#define __ERR_H

#ifndef ASSEMBLY
#include <sys/types.h> // for status_t
#endif

#define NO_ERROR                (0)

// Internal failures
// TODO: Rename ERR_GENERIC -> ERR_INTERNAL
#define ERR_GENERIC             (-1)
#define ERR_NOT_SUPPORTED       (-2)
#define ERR_NOT_FOUND           (-3)
#define ERR_NO_MEMORY           (-4)
#define ERR_NO_RESOURCES        (-5)

// Parameter errors
#define ERR_BAD_SYSCALL         (-10)
#define ERR_BAD_HANDLE          (-11)
#define ERR_INVALID_ARGS        (-12)
#define ERR_OUT_OF_RANGE        (-13)
#define ERR_NOT_ENOUGH_BUFFER   (-14)
#define ERR_ALREADY_EXISTS      (-16)

// Precondition or state errors
#define ERR_BAD_STATE           (-20)
#define ERR_NOT_READY           (-21)
#define ERR_TIMED_OUT           (-22)
#define ERR_BUSY                (-23)
#define ERR_CANCELLED           (-24)
#define ERR_CHANNEL_CLOSED      (-25)

// Permission check errors
#define ERR_ACCESS_DENIED       (-30)

// Input-output errors
#define ERR_IO                  (-40)
// TODO: Make more generic (ERR_IO_NACK?)
#define ERR_I2C_NACK            (-41)
// TODO: Rename to something more generic like ERR_DATA_INTEGRITY
#define ERR_CHECKSUM_FAIL       (-42)

// Filesystem specific errors
#define ERR_BAD_PATH            (-50)
#define ERR_NOT_DIR             (-51)
#define ERR_NOT_FILE            (-52)
// TODO: Confusing name - is this the same as POSIX ELOOP?
#define ERR_RECURSE_TOO_DEEP    (-53)

// Garbage bin
// TODO: Replace with INVALID_ARGS
#define ERR_NOT_VALID           (-91)

// TODO: Should just be NOT_SUPPORTED
#define ERR_NOT_IMPLEMENTED     (-92)

// TODO: Replace with ERR_INVALID_ARGS or ERR_NOT_ENOUGH_BUFFER
#define ERR_TOO_BIG             (-93)

// TODO: This appears to be obsolete, see if we need it.
#define ERR_NOT_CONFIGURED      (-94)

// TODO: This appears to be used as a bool, does it need a distinct code?
#define ERR_FAULT               (-95)

// TODO: Replace with either ACCESS_DENIED or NOT_SUPPORTED as appropriate
#define ERR_NOT_ALLOWED         (-96)

// TODO: These all seem like state errors, should they just be ERR_BAD_STATE?
#define ERR_NO_MSG              (-98)
#define ERR_ALREADY_STARTED     (-99)
#define ERR_NOT_BLOCKED         (-100)
#define ERR_THREAD_DETACHED     (-101)

// TODO: These two are variants of ERR_BAD_STATE
#define ERR_ALREADY_MOUNTED     (-103)
#define ERR_NOT_MOUNTED         (-104)

// TODO: One user of this code, remove it.
#define ERR_PARTIAL_WRITE       (-105)

#define ERR_USER_BASE           (-16384)

#endif
