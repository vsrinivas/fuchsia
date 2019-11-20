// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/**
 * @fileoverview A collection of utilities for manipulating elements of namespaces (e.g., files)
 */

import * as fdio from 'fdio'

import * as util from './util.js';

fidl.loadLibrary('fuchsia.io');

class Dirent {
  /**
   * Constructs a Dirent.
   * @param {BigInt} ino The inode of the dirent.
   * @param {Number} type (a uint8)
   * @param {String} name The name of the dirent.
   */
  constructor(ino, type, name) {
    this.ino = ino;
    this.type = type;
    this.name = name;
  }
}

// A class that provides a more gentle interface when you have a handle to a fuchsia.io.Directory.
class Directory {
  /**
   * Constructs the Directory object.
   * @param {zx.Channel} nodeClientChannel a zx.Channel where the other endpoint is a
   *     fuchsia.io.Directory.
   * @param {boolean} shouldClose Whether invocations of the close() method should close the
   *     directory (i.e., whether this object takes ownership of the channel).
   */
  constructor(nodeClientChannel, shouldClose) {
    this._pathClient = new fidl.ProtocolClient(nodeClientChannel, fidling.fuchsia_io.Directory);
    this._shouldClose = shouldClose;
  }

  /**
   * Close the underlying channel, if specified when invoking the constructor.
   */
  close() {
    if (this._shouldClose) {
      fidl.ProtocolClient.close(this._pathClient);
    }
  }

  /**
   * Returns a list of Dirent objects representing the contents of the directory.
   */
  async readDirEnts() {
    let ents = undefined;
    let res = [];
    while (true) {
      ents = await this._pathClient.ReadDirents(fidling.fuchsia_io.MAX_BUF);
      if (ents.s != '0') {
        throw 'Unable to read directory entries';
      }
      if (ents.dirents.length == 0) {
        break;
      }
      let buf = new ArrayBuffer(ents.dirents.length);
      let view = new DataView(buf);
      // First, convert from strings to bytes.  fidlcat should really do that for us.
      for (let i = 0; i < ents.dirents.length; i++) {
        view.setUint8(i, parseInt(ents.dirents[i]));
      }
      // Then, decode the array values
      for (let i = 0; i < ents.dirents.length;) {
        let ino = view.getBigInt64(i, true);
        i += 8;
        let size = view.getUint8(i);
        i += 1;
        let type = view.getUint8(i);
        i += 1;
        let name = util.decodeUtf8(new DataView(buf, i, size));
        i += size;
        let entry = new Dirent(ino, type, name);
        res.push(entry);
      }
    }
    let status = await this._pathClient.Rewind();
    if (status.s != '0') {
      throw 'Unable to rewind directory';
    }
    return res;
  }

  // Given an absolute path string, returns a Directory object representing that path.
  // If the path string represents a non-directory node, returns null.
  static async getDirectoryFor(pathString) {
    let rns = fdio.nsExportRoot();
    let splitDirString = pathString.split('/');
    const rootElements = rns.getElements();

    // The root elements are special, top-level paths.  Every path is relative to one.
    // So, we have to start looking for the given path from one of the root strings.
    // Iterate until we find the right one.
    for (let element in rootElements) {
      let splitElement = element.split('/');
      let handle = undefined;
      let pathClient = undefined;
      if (util.arraysEqual(splitDirString, splitElement)) {
        // Pretend for the moment that all of the elements of the root ns
        // object are Directories.  We are trying to get the NodeInfo for one: fake it.
        handle = rootElements[element].handle;
        return new Directory(new zx.Channel(handle), false);
      } else if (util.isPrefixOf(splitElement, splitDirString)) {
        // We are trying to list something reachable from a root handle.
        let rootHandle = rootElements[element].handle;
        let rootChannel = new zx.Channel(rootHandle);
        let dirClient = new fidl.ProtocolClient(rootChannel, fidling.fuchsia_io.Directory);

        // element.length + 1 gets rid of the next path separator.
        // Is this right?  Are all of the "mount points" in the root ns?
        const restOfPath = pathString.substring(element.length + 1);

        const request = new fidl.Request(fidling.fuchsia_io.Node);
        pathClient = request.getProtocolClient();
        let openedPromise = pathClient.OnOpen((args) => {
          return args;
        });
        dirClient.Open(
            fidling.fuchsia_io.OPEN_RIGHT_READABLE | fidling.fuchsia_io.OPEN_FLAG_DESCRIBE, 0,
            restOfPath, request.getChannelForServer());
        let args = await openedPromise;
        // TODO: check the value of args.s
        if ('directory' in args.info) {
          return new Directory(pathClient, true);
        }
        fidl.ProtocolClient.close(request.getProtocolClient());
      }
    }
    throw 'Node ' + pathString + ' not found';
  }
}

/**
 * Returns a listing of a directory.  Type TBD, but currently an array of Dirents.
 *
 * @param {String} pathString A path we want listed. (Currently, must be an
 *     absolute directory.  We'll fix that.)
 */
async function ls(pathString) {
  let directory = await Directory.getDirectoryFor(pathString);
  if (directory == null) {
    throw 'Unknown directory';
  }
  let dirents = await directory.readDirEnts();
  directory.close();
  return dirents;
}

(async function(global) {
ls('/svc')
    .then((svcDir) => {
      // Make services available on an object called `svc`
      const svc = {};
      const svcNames = [];
      for (const service of svcDir) {
        // serviceName looks like "fuchsia.boot.RootJob"
        const serviceName = service.name;
        // Mangle service names to be valid JS identifiers
        // proxyName looks like "fuchsia_boot_RootJob"
        const proxyName = serviceName.replace(/\./g, '_');
        svcNames.push(proxyName);
        const idx = serviceName.lastIndexOf('.');
        // name looks like "RootJob"
        const name = serviceName.substr(idx + 1);
        // libraryName looks like "fuchsia.boot"
        const libraryName = serviceName.substr(0, idx);
        // Define a getter that connects to the service
        // TODO: should this cache connections until their handles close?
        Object.defineProperty(svc, proxyName, {
          enumerable: true,
          get: () => {
            return new fidl.ProtocolClient(
                new zx.Channel(fdio.serviceConnect(`/svc/${serviceName}`)),
                `${libraryName}/${name}`);
          },
        });
      }
      svc[Symbol.for('completions')] = svcNames;
      global['svc'] = svc;
    })
    .catch((e) => {
      console.log(e);
      console.log(e.stack);
    });
})(globalThis);

export {ls};
