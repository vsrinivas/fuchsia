# Copyright 2014 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

# distutils: language = c++

from cpython.buffer cimport PyBUF_CONTIG
from cpython.buffer cimport PyBUF_CONTIG_RO
from cpython.buffer cimport Py_buffer
from cpython.buffer cimport PyBuffer_FillInfo
from cpython.buffer cimport PyBuffer_Release
from cpython.buffer cimport PyObject_GetBuffer
from cpython.mem cimport PyMem_Malloc, PyMem_Free
from libc.stdint cimport int32_t, int64_t, uint32_t, uint64_t, uintptr_t

cdef extern from "mojo/system/result.h" nogil:
  ctypedef int32_t MojoResult
  const MojoResult MOJO_RESULT_OK
  const MojoResult MOJO_SYSTEM_RESULT_CANCELLED
  const MojoResult MOJO_SYSTEM_RESULT_UNKNOWN
  const MojoResult MOJO_SYSTEM_RESULT_INVALID_ARGUMENT
  const MojoResult MOJO_SYSTEM_RESULT_BAD_HANDLE
  const MojoResult MOJO_SYSTEM_RESULT_WRONG_TYPE
  const MojoResult MOJO_SYSTEM_RESULT_DEADLINE_EXCEEDED
  const MojoResult MOJO_SYSTEM_RESULT_NOT_FOUND
  const MojoResult MOJO_SYSTEM_RESULT_ALREADY_EXISTS
  const MojoResult MOJO_SYSTEM_RESULT_PERMISSION_DENIED
  const MojoResult MOJO_SYSTEM_RESULT_RESOURCE_EXHAUSTED
  const MojoResult MOJO_SYSTEM_RESULT_FAILED_PRECONDITION
  const MojoResult MOJO_SYSTEM_RESULT_BUSY
  const MojoResult MOJO_SYSTEM_RESULT_ABORTED
  const MojoResult MOJO_SYSTEM_RESULT_OUT_OF_RANGE
  const MojoResult MOJO_SYSTEM_RESULT_UNIMPLEMENTED
  const MojoResult MOJO_SYSTEM_RESULT_INTERNAL
  const MojoResult MOJO_SYSTEM_RESULT_UNAVAILABLE
  const MojoResult MOJO_SYSTEM_RESULT_SHOULD_WAIT
  const MojoResult MOJO_SYSTEM_RESULT_DATA_LOSS

cdef extern from "mojo/system/handle.h" nogil:
  ctypedef uint32_t MojoHandle
  const MojoHandle MOJO_HANDLE_INVALID

  ctypedef uint32_t MojoHandleSignals
  const MojoHandleSignals MOJO_HANDLE_SIGNAL_NONE
  const MojoHandleSignals MOJO_HANDLE_SIGNAL_READABLE
  const MojoHandleSignals MOJO_HANDLE_SIGNAL_WRITABLE
  const MojoHandleSignals MOJO_HANDLE_SIGNAL_PEER_CLOSED

  cdef struct MojoHandleSignalsState:
    MojoHandleSignals satisfied_signals
    MojoHandleSignals satisfiable_signals

  MojoResult MojoClose(MojoHandle handle)

cdef extern from "mojo/system/time.h" nogil:
  ctypedef int64_t MojoTimeTicks

  ctypedef uint64_t MojoDeadline
  const MojoDeadline MOJO_DEADLINE_INDEFINITE

  MojoTimeTicks MojoGetTimeTicksNow()

cdef extern from "mojo/system/wait.h" nogil:
  MojoResult MojoWait "MojoWait"(MojoHandle handle,
                                 MojoHandleSignals signals,
                                 MojoDeadline deadline,
                                 MojoHandleSignalsState* signals_state)
  MojoResult MojoWaitMany "MojoWaitMany"(const MojoHandle* handles,
                                         const MojoHandleSignals* signals,
                                         uint32_t num_handles,
                                         MojoDeadline deadline,
                                         uint32_t* result_index,
                                         MojoHandleSignalsState* signals_states)

cdef extern from "mojo/system/message_pipe.h" nogil:
  ctypedef uint32_t MojoCreateMessagePipeOptionsFlags
  const MojoCreateMessagePipeOptionsFlags MOJO_CREATE_MESSAGE_PIPE_OPTIONS_FLAG_NONE

  ctypedef uint32_t MojoWriteMessageFlags
  const MojoWriteMessageFlags MOJO_WRITE_MESSAGE_FLAG_NONE

  ctypedef uint32_t MojoReadMessageFlags
  const MojoReadMessageFlags MOJO_READ_MESSAGE_FLAG_NONE
  const MojoReadMessageFlags MOJO_READ_MESSAGE_FLAG_MAY_DISCARD

  cdef struct MojoCreateMessagePipeOptions:
    uint32_t struct_size
    MojoCreateMessagePipeOptionsFlags flags

  MojoResult MojoCreateMessagePipe(
      const MojoCreateMessagePipeOptions* options,
      MojoHandle* message_pipe_handle0,
      MojoHandle* message_pipe_handle1)

  MojoResult MojoWriteMessage(
      MojoHandle message_pipe_handle,
      const void* bytes,
      uint32_t num_bytes,
      const MojoHandle* handles,
      uint32_t num_handles,
      MojoWriteMessageFlags flags)

  MojoResult MojoReadMessage(
      MojoHandle message_pipe_handle,
      void* bytes,
      uint32_t* num_bytes,
      MojoHandle* handles,
      uint32_t* num_handles,
      MojoReadMessageFlags flags)

cdef extern from "mojo/system/data_pipe.h" nogil:
  ctypedef uint32_t MojoCreateDataPipeOptionsFlags
  const MojoCreateDataPipeOptionsFlags MOJO_CREATE_DATA_PIPE_OPTIONS_FLAG_NONE

  cdef struct MojoCreateDataPipeOptions:
    uint32_t struct_size
    MojoCreateDataPipeOptionsFlags flags
    uint32_t element_num_bytes
    uint32_t capacity_num_bytes

  ctypedef uint32_t MojoWriteDataFlags

  const MojoWriteDataFlags MOJO_WRITE_DATA_FLAG_NONE
  const MojoWriteDataFlags MOJO_WRITE_DATA_FLAG_ALL_OR_NONE

  ctypedef uint32_t MojoReadDataFlags

  const MojoReadDataFlags MOJO_READ_DATA_FLAG_NONE
  const MojoReadDataFlags MOJO_READ_DATA_FLAG_ALL_OR_NONE
  const MojoReadDataFlags MOJO_READ_DATA_FLAG_DISCARD
  const MojoReadDataFlags MOJO_READ_DATA_FLAG_QUERY
  const MojoReadDataFlags MOJO_READ_DATA_FLAG_PEEK

  MojoResult MojoCreateDataPipe(
      const MojoCreateDataPipeOptions* options,
      MojoHandle* data_pipe_producer_handle,
      MojoHandle* data_pipe_consumer_handle)

  MojoResult MojoWriteData(
      MojoHandle data_pipe_producer_handle,
      const void* elements,
      uint32_t* num_bytes,
      MojoWriteDataFlags flags)

  MojoResult MojoBeginWriteData(
      MojoHandle data_pipe_producer_handle,
      void** buffer,
      uint32_t* buffer_num_bytes,
      MojoWriteDataFlags flags)

  MojoResult MojoEndWriteData(
      MojoHandle data_pipe_producer_handle,
      uint32_t num_bytes_written)

  MojoResult MojoReadData(
      MojoHandle data_pipe_consumer_handle,
      void* elements,
      uint32_t* num_bytes,
      MojoReadDataFlags flags)

  MojoResult MojoBeginReadData(
      MojoHandle data_pipe_consumer_handle,
      const void** buffer,
      uint32_t* buffer_num_bytes,
      MojoReadDataFlags flags)

  MojoResult MojoEndReadData(
      MojoHandle data_pipe_consumer_handle,
      uint32_t num_bytes_read)

cdef extern from "mojo/system/buffer.h" nogil:
  ctypedef uint32_t MojoCreateSharedBufferOptionsFlags
  const MojoCreateSharedBufferOptionsFlags MOJO_CREATE_SHARED_BUFFER_OPTIONS_FLAG_NONE

  cdef struct MojoCreateSharedBufferOptions:
    uint32_t struct_size
    MojoCreateSharedBufferOptionsFlags flags

  ctypedef uint32_t MojoDuplicateBufferHandleOptionsFlags
  const MojoDuplicateBufferHandleOptionsFlags MOJO_DUPLICATE_BUFFER_HANDLE_OPTIONS_FLAG_NONE

  cdef struct MojoDuplicateBufferHandleOptions:
    uint32_t struct_size
    MojoDuplicateBufferHandleOptionsFlags flags

  ctypedef uint32_t MojoMapBufferFlags
  const MojoMapBufferFlags MOJO_MAP_BUFFER_FLAG_NONE

  MojoResult MojoCreateSharedBuffer(
      const MojoCreateSharedBufferOptions* options,
      uint64_t num_bytes,
      MojoHandle* shared_buffer_handle)

  MojoResult MojoDuplicateBufferHandle(
      MojoHandle buffer_handle,
      const MojoDuplicateBufferHandleOptions* options,
      MojoHandle* new_buffer_handle)

  MojoResult MojoMapBuffer(MojoHandle buffer_handle,
                           uint64_t offset,
                           uint64_t num_bytes,
                           void** buffer,
                           MojoMapBufferFlags flags)

  MojoResult MojoUnmapBuffer(void* buffer)
