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
   */
  constructor(nodeClientChannel) {
    this._pathClient = new fidl.ProtocolClient(nodeClientChannel, fidling.fuchsia_io.Directory);
  }

  /**
   * Close the underlying channel, if specified when invoking the constructor.
   */
  close() {
    fidl.ProtocolClient.close(this._pathClient);
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
    let restOfPath = undefined;
    let element = undefined;
    let checkDirectory = fidling.fuchsia_io.OPEN_FLAG_DESCRIBE;

    // The root elements are special, top-level paths.  Every path is relative to one.
    // So, we have to start looking for the given path from one of the root strings.
    // Iterate until we find the right one.
    for (let e in rootElements) {
      let splitElement = e.split('/');
      if (util.arraysEqual(splitDirString, splitElement)) {
        restOfPath = '.';
        element = e;
        // For some reason, waiting for OnOpen below in this code path
        // causes memory corruption.  We don't really need the check,
        // and we're likely to deprecate this code anyway.
        checkDirectory = 0;
      } else if (util.isPrefixOf(splitElement, splitDirString)) {
        // We are trying to list something reachable from a root handle.
        // element.length + 1 gets rid of the next path separator.
        restOfPath = pathString.substring(e.length + 1);
        element = e;
      }
    }

    if (typeof element == 'undefined') {
      throw 'Node ' + pathString + ' not found';
    }
    // Each Node (i.e., the thing we want listed) is contained within a root namespace
    // For example, if you want to list /bin/ls, it is going to be the ls node inside
    // the /bin root element.  |element| is the root namespace element.
    // rootChannel is a channel to the service providing the root namespace element.
    let rootHandle = rootElements[element].handle;
    let rootChannel = new zx.Channel(rootHandle);

    // dirClient is a wrapper that lets you speak the fuchsia.io.Directory protocol
    // to the root namespace.
    let dirClient = new fidl.ProtocolClient(rootChannel, fidling.fuchsia_io.Directory);

    // When we want to interact with the node within the root namespace (this
    // would be "ls" if you are opening "/bin/ls"), you need to create a
    // channel to speak to it using the fuchsia.io.Node protocol.  The way we
    // set up that channel is to call the Open() method of the root namespace
    // directory ("/bin"), and send it |restOfPath| ("ls") and one end of a
    // channel. We can then communicate via that Node by speaking
    // fuchsia.io.Node over the other end of that channel.
    // |request| is a wrapper that gives you two ends of a channel.
    const request = new fidl.Request(fidling.fuchsia_io.Node);
    // |nodeClient| is going to be the end of the channel that we speak Node over.
    let nodeClient = request.getProtocolClient();
    let openedPromise = undefined;
    if (checkDirectory != 0) {
      // We can get notified when the Node gets opened.
      openedPromise = nodeClient.OnOpen((args) => {
        return args;
      });
    }
    // Ask the service providing the root namespace element to open the Node
    // we want to inspect.
    dirClient.Open(
        fidling.fuchsia_io.OPEN_RIGHT_READABLE | checkDirectory, 0, restOfPath,
        request.getChannelForServer());
    if (checkDirectory != 0) {
      let args = await openedPromise;
      // TODO: check the value of args.s
      if ('directory' in args.info) {
        return new Directory(nodeClient);
      } else {
        throw pathString + " is not a directory"
      }
    } else {
      return new Directory(nodeClient);
    }
    fidl.ProtocolClient.close(request.getProtocolClient());
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
          // serviceName looks like "fuchsia.kernel.RootJob"
          const serviceName = service.name;
          // Mangle service names to be valid JS identifiers
          // proxyName looks like "fuchsia_kernel_RootJob"
          const proxyName = serviceName.replace(/\./g, '_');
          svcNames.push(proxyName);
          const idx = serviceName.lastIndexOf('.');
          // name looks like "RootJob"
          const name = serviceName.substr(idx + 1);
          // libraryName looks like "fuchsia.kernel"
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
