// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

part of core;

typedef void VoidCallback();

// These values are from these headers:
//
//  * https://fuchsia.googlesource.com/magenta/+/master/system/public/magenta/errors.h
//  * https://fuchsia.googlesource.com/magenta/+/master/system/public/magenta/types.h

// Errors

const int NO_ERROR = 0;
const int ERR_INTERNAL = -1;
const int ERR_NOT_SUPPORTED = -2;
const int ERR_NO_RESOURCES = -5;
const int ERR_NO_MEMORY = -4;
const int ERR_INVALID_ARGS = -10;
const int ERR_WRONG_TYPE = -54;
const int ERR_BAD_SYSCALL = -11;
const int ERR_BAD_HANDLE = -12;
const int ERR_OUT_OF_RANGE = -13;
const int ERR_BUFFER_TOO_SMALL = -14;
const int ERR_BAD_STATE = -20;
const int ERR_NOT_FOUND = -3;
const int ERR_ALREADY_EXISTS = -15;
const int ERR_ALREADY_BOUND = -16;
const int ERR_TIMED_OUT = -23;
const int ERR_HANDLE_CLOSED = -24;
const int ERR_REMOTE_CLOSED = -25;
const int ERR_UNAVAILABLE = -26;
const int ERR_SHOULD_WAIT = -27;
const int ERR_ACCESS_DENIED = -30;
const int ERR_IO = -40;
const int ERR_IO_REFUSED = -41;
const int ERR_IO_DATA_INTEGRITY = -42;
const int ERR_IO_DATA_LOSS = -43;
const int ERR_BAD_PATH = -50;
const int ERR_NOT_DIR = -51;
const int ERR_NOT_FILE = -52;

String getStringForStatus(int status) {
  switch (status) {
    case NO_ERROR:
      return 'NO_ERROR';
    case ERR_INTERNAL:
      return 'ERR_INTERNAL';
    case ERR_NOT_SUPPORTED:
      return 'ERR_NOT_SUPPORTED';
    case ERR_NO_RESOURCES:
      return 'ERR_NO_RESOURCES';
    case ERR_NO_MEMORY:
      return 'ERR_NO_MEMORY';
    case ERR_INVALID_ARGS:
      return 'ERR_INVALID_ARGS';
    case ERR_WRONG_TYPE:
      return 'ERR_WRONG_TYPE';
    case ERR_BAD_SYSCALL:
      return 'ERR_BAD_SYSCALL';
    case ERR_BAD_HANDLE:
      return 'ERR_BAD_HANDLE';
    case ERR_OUT_OF_RANGE:
      return 'ERR_OUT_OF_RANGE';
    case ERR_BUFFER_TOO_SMALL:
      return 'ERR_BUFFER_TOO_SMALL';
    case ERR_BAD_STATE:
      return 'ERR_BAD_STATE';
    case ERR_NOT_FOUND:
      return 'ERR_NOT_FOUND';
    case ERR_ALREADY_EXISTS:
      return 'ERR_ALREADY_EXISTS';
    case ERR_ALREADY_BOUND:
      return 'ERR_ALREADY_BOUND';
    case ERR_TIMED_OUT:
      return 'ERR_TIMED_OUT';
    case ERR_HANDLE_CLOSED:
      return 'ERR_HANDLE_CLOSED';
    case ERR_REMOTE_CLOSED:
      return 'ERR_REMOTE_CLOSED';
    case ERR_UNAVAILABLE:
      return 'ERR_UNAVAILABLE';
    case ERR_SHOULD_WAIT:
      return 'ERR_SHOULD_WAIT';
    case ERR_ACCESS_DENIED:
      return 'ERR_ACCESS_DENIED';
    case ERR_IO:
      return 'ERR_IO';
    case ERR_IO_REFUSED:
      return 'ERR_IO_REFUSED';
    case ERR_IO_DATA_INTEGRITY:
      return 'ERR_IO_DATA_INTEGRITY';
    case ERR_IO_DATA_LOSS:
      return 'ERR_IO_DATA_LOSS';
    case ERR_BAD_PATH:
      return 'ERR_BAD_PATH';
    case ERR_NOT_DIR:
      return 'ERR_NOT_DIR';
    case ERR_NOT_FILE:
      return 'ERR_NOT_FILE';
    default:
      return '(unknown: $status)';
  }
}

const int MX_HANDLE_INVALID    = 0;

const int MX_TIME_INFINITE     = -1;

// Signals

const int MX_SIGNAL_NONE       = 0;
const int MX_OBJECT_SIGNAL_ALL = 0x00ffffff;
const int MX_USER_SIGNAL_ALL   = 0xff000000;

const int MX_OBJECT_SIGNAL_0   = 1 << 0;
const int MX_OBJECT_SIGNAL_1   = 1 << 1;
const int MX_OBJECT_SIGNAL_2   = 1 << 2;
const int MX_OBJECT_SIGNAL_3   = 1 << 3;
const int MX_OBJECT_SIGNAL_4   = 1 << 4;
const int MX_OBJECT_SIGNAL_5   = 1 << 5;
const int MX_OBJECT_SIGNAL_6   = 1 << 6;
const int MX_OBJECT_SIGNAL_7   = 1 << 7;
const int MX_OBJECT_SIGNAL_8   = 1 << 8;
const int MX_OBJECT_SIGNAL_9   = 1 << 9;
const int MX_OBJECT_SIGNAL_10  = 1 << 10;
const int MX_OBJECT_SIGNAL_11  = 1 << 11;
const int MX_OBJECT_SIGNAL_12  = 1 << 12;
const int MX_OBJECT_SIGNAL_13  = 1 << 13;
const int MX_OBJECT_SIGNAL_14  = 1 << 14;
const int MX_OBJECT_SIGNAL_15  = 1 << 15;
const int MX_OBJECT_SIGNAL_16  = 1 << 16;
const int MX_OBJECT_SIGNAL_17  = 1 << 17;
const int MX_OBJECT_SIGNAL_18  = 1 << 18;
const int MX_OBJECT_SIGNAL_19  = 1 << 19;
const int MX_OBJECT_SIGNAL_20  = 1 << 20;
const int MX_OBJECT_SIGNAL_21  = 1 << 21;
const int MX_OBJECT_SIGNAL_22  = 1 << 22;
const int MX_OBJECT_SIGNAL_23  = 1 << 23;

const int MX_USER_SIGNAL_0     = 1 << 24;
const int MX_USER_SIGNAL_1     = 1 << 25;
const int MX_USER_SIGNAL_2     = 1 << 26;
const int MX_USER_SIGNAL_3     = 1 << 27;
const int MX_USER_SIGNAL_4     = 1 << 28;
const int MX_USER_SIGNAL_5     = 1 << 29;
const int MX_USER_SIGNAL_6     = 1 << 30;
const int MX_USER_SIGNAL_7     = 1 << 31;

const int MX_SIGNAL_HANDLE_CLOSED = MX_OBJECT_SIGNAL_23;

// Event
const int MX_EVENT_SIGNALED         = MX_OBJECT_SIGNAL_3;
const int MX_EVENT_SIGNAL_MASK      = MX_USER_SIGNAL_ALL | MX_OBJECT_SIGNAL_3;

// EventPair
const int MX_EPAIR_SIGNALED         = MX_OBJECT_SIGNAL_3;
const int MX_EPAIR_CLOSED           = MX_OBJECT_SIGNAL_2;
const int MX_EPAIR_SIGNAL_MASK      = MX_USER_SIGNAL_ALL | MX_OBJECT_SIGNAL_2 | MX_OBJECT_SIGNAL_3;

// Task signals (process, thread, job)
const int MX_TASK_SIGNAL_TERMINATED = MX_OBJECT_SIGNAL_3;
const int MX_TASK_SIGNAL_MASK       = MX_OBJECT_SIGNAL_3;

// Channel
const int MX_CHANNEL_READABLE       = MX_OBJECT_SIGNAL_0;
const int MX_CHANNEL_WRITABLE       = MX_OBJECT_SIGNAL_1;
const int MX_CHANNEL_PEER_CLOSED    = MX_OBJECT_SIGNAL_2;

// Socket
const int MX_SOCKET_READABLE        = MX_OBJECT_SIGNAL_0;
const int MX_SOCKET_WRITABLE        = MX_OBJECT_SIGNAL_1;
const int MX_SOCKET_PEER_CLOSED     = MX_OBJECT_SIGNAL_2;

// Data pipe
const int MX_DATAPIPE_READABLE          = MX_OBJECT_SIGNAL_0;
const int MX_DATAPIPE_WRITABLE          = MX_OBJECT_SIGNAL_1;
const int MX_DATAPIPE_PEER_CLOSED       = MX_OBJECT_SIGNAL_2;
const int MX_DATAPIPE_READ_THRESHOLD    = MX_OBJECT_SIGNAL_4;
const int MX_DATAPIPE_WRITE_THRESHOLD   = MX_OBJECT_SIGNAL_5;

// Legacy signal names, to be removed.
const int MX_SIGNAL_READABLE        = MX_OBJECT_SIGNAL_0;
const int MX_SIGNAL_WRITABLE        = MX_OBJECT_SIGNAL_1;
const int MX_SIGNAL_PEER_CLOSED     = MX_OBJECT_SIGNAL_2;
const int MX_SIGNAL_SIGNALED        = MX_OBJECT_SIGNAL_3;
