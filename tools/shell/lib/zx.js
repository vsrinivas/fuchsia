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
    throw "Cannot call duplicate on pure object";
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
}

const ZX_CHANNEL_READABLE = zx_internal.ZX_CHANNEL_READABLE;
const ZX_CHANNEL_PEER_CLOSED = zx_internal.ZX_CHANNEL_PEER_CLOSED;
const ZX_RIGHT_SAME_RIGHTS = zx_internal.ZX_RIGHT_SAME_RIGHTS;

global['zx'] = {
  Object,
  Channel,

  ZX_CHANNEL_READABLE,
  ZX_CHANNEL_PEER_CLOSED,
  ZX_RIGHT_SAME_RIGHTS,
}
})(globalThis);
