// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/**
 * @fileoverview A collection of utilities for manipulating elements of namespaces (e.g., files)
 */

import * as os from "os";

/**
* Returns the real NS path of a given path under current environment.
*
* @param {String} pathString A path we want to access. (Must be an
*     absolute directory.)
*/
function getPath(path) {
  var ns_path = '/ns' + path;
  var [stat, ret] = os.lstat(ns_path);
  if (ret === 0 && (parseInt(os.S_IFDIR & stat.mode) > 0)) {
    return ns_path;
  } else {
    return path;
  }
}

/**
* Returns a listing of a directory. The returned values are the names of entries.
*
* @param {String} pathString A path we want listed. (Currently, must be an
*     absolute directory.  We'll fix that.)
*/
function ls(path) {
  var [content, res] = os.readdir(path);
  if (res === 0) {
    return content;
  } else {
    return null;
  }
}

(function(global) {
  var svc = {};
  var svcCompletionNames = [];

  const svcDir = getPath('/svc');
  const serviceNames = ls(svcDir);

  if (serviceNames != null) {
    for (const serviceName of serviceNames) {
      if (serviceName === '.') continue;

      // serviceName looks like "fuchsia.kernel.RootJob"
      // Mangle service names to be valid JS identifiers
      // proxyName looks like "fuchsia_kernel_RootJob"
      const proxyName = serviceName.replace(/\./g, '_');
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
              new zx.Channel(fdio.serviceConnect(`${svcDir}/${serviceName}`)),
              `${libraryName}/${name}`);
        },
      });
      svcCompletionNames.push(proxyName);
    }
  }

  svc[Symbol.for('completions')] = svcCompletionNames;
  global['svc'] = svc;
})(globalThis);

export { getPath, ls };