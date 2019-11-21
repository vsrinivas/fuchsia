// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/**
 * @fileoverview Utilities for interacting with Zircon objects.
 */

import * as zx_internal from 'zx_internal';

(function(global) {

/**
 * Wraps and takes ownership of a handle to an object.
 */
class Object {
  /**
   * Constructor for zx.Object.
   * @param {handle} handle (as returned by, e.g., zx.channelCreate()).  Will be
   *     deleted by a call to close()
   */
  constructor(handle) {
    this._handle = handle;
  }

  /**
   * The zx_object_wait_async() call.
   * @param {int32} flags The set of signals
   * @param {callback} callback The code to invoke when you are done waiting.
   */
  waitAsync(flags, callback) {
    zx_internal.objectWaitAsync(this._handle, flags, callback);
  }

  /**
   * Closes the underlying handle.
   */
  close() {
    zx_internal.handleClose(this._handle);
  }

  /**
   * Duplicates the underlying handle: do not call on zx.Object, only on a subclass.
   */
  duplicate(rights) {
    // Fix this if you can figure out how to get a new object of the type of this.
    throw 'Cannot call duplicate on pure object';
  }

  /**
   * The zx_object_get_child() call.  Returns a zx.Object that is a child of this object
   * with the specified koid.
   * @param {Number} koid The koid of the child object.
   * @param {Number} rights The rights on the child object.
   */
  getChild(koid, rights) {
    return zx_internal.getChild(this._handle, koid, rights);
  }

  /**
   * Returns an object with fields corresponding to the result of
   * zx_object_get_info(ZX_INFO_HANDLE_BASIC). See the zx_object_get_info documentation for details.
   */
  getBasicInfo() {
    return zx_internal.getObjectInfo(this._handle, zx_internal.ZX_INFO_HANDLE_BASIC);
  }

  /**
   * Returns a given property of a kernel object.  See the documentation for
   * zx_object_get_property for details.
   *
   * The property ZX_PROP_NAME will be converted into a JS string.
   *
   * @param {Number} property An uint32 indicating which property to get/set.
   */
  getProperty(property) {
    return zx_internal.getObjectProperty(this._handle, property);
  }
}

/**
 * A Zircon object of type channel.
 */
class Channel extends Object {
  constructor(handle) {
    super(handle);
  }

  /**
   * Calls zx_channel_create()
   * @returns a two-element array, where element 0 is zx.Channel representing
   *     out0, and element 1 is zx.Channel representing out1.
   */
  static create() {
    const handles = zx_internal.channelCreate();
    return [new Channel(handles[0]), new Channel(handles[1])];
  }

  /**
   * Calls zx_channel_write.
   * @param {ArrayBuffer} bytes An ArrayBuffer with bytes to send over the channel.
   * @param {Handle} handles An Array of Handle objects.
   */
  write(bytes, handles) {
    let unpackedHandles = [];
    for (let i = 0; i < handles.length; i++) {
      unpackedHandles.push(handles[i]._handle);
    }
    zx_internal.channelWrite(this._handle, bytes, unpackedHandles)
  }

  /**
   * Calls zx_channel_read.  Returns an array with two elements, the first of
   * which is the bytes, and the second of which is the handles.
   * TODO(jeremymanson) We can do better than a two element array.  Also, the
   * first thing should be an ArrayBuffer, as we pass to write.
   */
  read() {
    return zx_internal.channelRead(this._handle);
  }

  duplicate(rights) {
    return new zx.Channel(zx_internal.duplicate(this._handle, rights));
  }

  /**
   * Returns a new zx.Channel object, where the handle is the numerical value passed in.
   * To be removed when fidl_codec gives us Handles instead of numbers.
   * @param {Number} value An int32 that represents a valid zx handle.
   */
  static fromValueDeprecated(value) {
    if (value == 0) {
      throw 'Null handle to Channel';
    }
    return new Channel(zx_internal.handleFromInt(value));
  }
}

/**
 * A Zircon object of type Task.
 */
class Task extends Object {
  constructor(handle) {
    super(handle);
  }
}

/**
 * A Zircon object of type Job (a kind of task).
 */
class Job extends Task {
  constructor(handle) {
    super(handle);
  }

  /**
   * Returns an object with fields corresponding to the result of
   * zx_object_get_info(ZX_INFO_JOB_CHILDREN). See the zx_object_get_info documentation for details.
   */
  getChildrenInfo() {
    return zx_internal.getObjectInfo(this._handle, zx_internal.ZX_INFO_JOB_CHILDREN);
  }

  /**
   * Returns an object with fields corresponding to the result of
   * zx_object_get_info(ZX_INFO_JOB_PROCESSES). See the zx_object_get_info documentation for
   * details.
   */
  getProcessesInfo() {
    return zx_internal.getObjectInfo(this._handle, zx_internal.ZX_INFO_JOB_PROCESSES)
  }

  /**
   * Returns a new zx.Job object, where the handle is the numerical value passed in.
   * To be removed when fidl_codec gives us Handles instead of numbers.
   * @param {Number} value An int32 that represents a valid zx handle.
   */
  static fromValueDeprecated(value) {
    if (value == 0) {
      throw 'Null handle to Job';
    }
    return new Job(zx_internal.handleFromInt(value));
  }

  /**
   * Returns the default zx.Job (i.e., zx_job_default())
   */
  static default() {
    return zx_internal.jobDefault();
  }
}

/**
 * A Zircon object of type Process (a kind of task).
 */
class Process extends Task {
  constructor(handle) {
    super(handle);
  }

  /**
   * Returns a zx.Process representing the current process.
   */
  self() {
    return new Process(zx_internal.processSelf());
  }
}

const ZX_CHANNEL_READABLE = zx_internal.ZX_CHANNEL_READABLE;
const ZX_CHANNEL_PEER_CLOSED = zx_internal.ZX_CHANNEL_PEER_CLOSED;
const ZX_RIGHT_SAME_RIGHTS = zx_internal.ZX_RIGHT_SAME_RIGHTS;
const ZX_PROP_NAME = zx_internal.ZX_PROP_NAME;

global['zx'] = {
  Object,
  Channel,
  Job,
  Process,

  ZX_CHANNEL_READABLE,
  ZX_CHANNEL_PEER_CLOSED,
  ZX_RIGHT_SAME_RIGHTS,
  ZX_PROP_NAME,
}
})(globalThis);
