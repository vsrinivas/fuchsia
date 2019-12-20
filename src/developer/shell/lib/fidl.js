// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/**
 * @fileoverview Utilities for making FIDL calls to protocols.  Contains both
 * classes for interacting with FIDL definitions and for calling out to
 * services.
 */
import * as fidl_internal from 'fidl_internal';

(function(global) {

// Classes and functions for managing FIDL IR and libraries

// TODO(jeremymanson): We really only want to look in the /pkg/ directory.
// This involves really turning the shell into a component, which we haven't
// done yet.
const IR_PATH = fidl_internal.irPath;

/**
 * Programmatic representation of a library.  Contains fidling IR.
 */
class Library {
  constructor(ir) {
    this.ir = ir;
    this.kinds = {
      bits: this.ir.bits_declarations,
      const : this.ir.const_declarations,
      enum: this.ir.enum_declarations,
      interface: this.ir.interface_declarations,
      service: this.ir.service_declarations,
      struct: this.ir.struct_declarations,
      table: this.ir.table_declarations,
      union: this.ir.union_declarations,
      xunion: this.ir.xunion_declarations,
      type_alias: this.ir.type_alias_declarations,
    }
  }

  /**
   * Given a particular protocol name (as in FIDL IR), return the IR in this
   * library that represents that protocol.
   */
  lookupProtocol(name) {
    const decl = this._findDeclaration(this.ir.interface_declarations, name);
    if (decl === null) {
      throw new Error(`No such protocol ${name}`);
    }
    return decl;
  }

  /**
   * Given a particular identifier name (as in FIDL IR), return the IR in this
   * library that represents that identifier.
   */
  lookupIdentifier(ident) {
    const kind = this.ir.declarations[ident];
    return [kind, this._findDeclaration(this.kinds[kind], ident)];
  }

  _findDeclaration(decls, name) {
    for (const decl of decls) {
      if (decl.name === name) {
        return decl;
      }
    }
    return null;
  }
};

const libraries = new Map();
// If we ever reassign this, we need to call close() to delete it.
var internalLibrary = null;

/**
 * Loads the IR for a given library.
 * libraryName should be of the form library.protocol
 *
 * Libraries are expected to be in a directory structure such that the filename
 * is the library name, with dots replaced by directory separators, ending with
 * .fidl.json.  This should be arranged by the build system.
 */
function loadIR(libraryName) {
  if (internalLibrary == null) {
    internalLibrary = fidl_internal.newLibrary();
  }

  let maybeSeparator = '';
  if (IR_PATH[IR_PATH.length - 1] != '/') {
    maybeSeparator = '/';
  }
  const irPath = IR_PATH + maybeSeparator + libraryName.replace(/\./g, '/') + '.fidl.json';

  // Load for JavaScript-level operations, like tab-completion and exposing the fidling.
  // We should probably move more of this to native code over time, so that we don't have to
  // load libraries twice.
  try {
    const f = std.open(irPath, 'r');
    let str = f.readAsString();
    f.close();
    // Hack: replace 64-bit ordinals with strings.  Better support for BigInts would be better.
    let regex = /("ordinal"\s*:\s*)([0-9]+)\s*,/gi;
    str = str.replace(regex, '$1"$2",');
    regex = /("generated_ordinal"\s*:\s*)([0-9]+)\s*,/gi;
    str = str.replace(regex, '$1"$2",');
    let ir = JSON.parse(str);
    libraries.set(ir.name, new Library(ir));
  } catch {
    throw 'FIDL definition for ' + libraryName + ' not found in ' + irPath;
  }

  // Load for C++ level operations, like encoding and decoding.
  if (!internalLibrary.loadLibrary(irPath)) {
    throw 'Internal error: Unable to load libraries for ' + libraryName;
  }
}

/**
 * Loads the IR for a given library.
 * libraryName should be of the form library.protocol
 *
 * Libraries are expected to be in a directory structure such that the filename
 * is the library name, with dots replaced by directory separators, ending with
 * .fidl.json.  This should be arranged by the build system.
 */
function loadLibrary(libraryName) {
  if (!libraries.has(libraryName)) {
    loadIR(libraryName);
  }
  if (!libraries.has(libraryName)) {
    throw new Error(`Could not find IR for ${libraryName}`);
  } else {
    return libraries.get(libraryName);
  }
}

/**
 * Loads the given FIDL JSON IR as a library.
 */
function loadLibraryIr(ir) {
  if (!libraries.has(ir.name)) {
    libraries.set(ir.name, new Library(ir));
  }
}

function lookupIdentifier(ident) {
  const [lib_name, _] = ident.split('/');
  const lib = loadLibrary(lib_name);
  return lib.lookupIdentifier(ident);
}

/**
 * Returns the value of a given FIDL IR attribute.
 * @param {*} decl The IR containing the attribute.
 * @param {*} name The name of the attribute to return
 */
function attribute(decl, name) {
  const attribs = decl.maybe_attributes;
  if (typeof attribs === 'undefined') {
    return;
  }
  for (const attrib of attribs) {
    if (attrib.name == name) {
      return attrib.value;
    }
  }
}

/**
 * Returns the fidldoc for a given method.
 * @param {*} method The IR of the method for which you want help.
 */
function methodHelp(method) {
  let help = attribute(method, 'Doc') || '';

  function typeHelp(type) {
    switch (type.kind) {
      case 'primitive':
        return type.subtype;
      case 'string':
        return 'string' + (type.maybe_element_count ? `:${type.maybe_element_count}` : '') +
            (type.nullable ? '?' : '');
      case 'vector':
        return `vector<${typeHelp(type.element_type)}>` +
            (type.maybe_element_count ? `:${type.maybe_element_count}` : '') +
            (type.nullable ? '?' : '');
      case 'array':
        return `array<${typeHelp(type.element_type)}>:${type.element_count}` +
            (type.nullable ? '?' : '');
      case 'identifier':
        return type.identifier + (type.nullable ? '?' : '');
      case 'request':
        return `request<${type.subtype}>` + (type.nullable ? '?' : '');
      case 'handle':
        return (type.subtype === 'handle' ? 'handle' : `handle<${type.subtype}>`) +
            (type.nullable ? '?' : '');
      default:
        return `UNKNOWN(${JSON.stringify(type)})`;
    }
  }

  function argHelp(arg) {
    return `${typeHelp(arg.type)} ${arg.name}`;
  }

  function argsHelp(args) {
    return '(' + args.map(argHelp).join(', ') + ')';
  }

  help = help + method.name;

  if (method.has_request) {
    help += argsHelp(method.maybe_request);
  }

  if (method.has_response) {
    help = help + ' -> ' + argsHelp(method.maybe_response);
  }

  return help + '\n';
}

// Classes and libraries for communicating with FIDL services.

const MSG_MAX_BYTES = 64 * 1024;

/**
 * Represents a FIDL message header.
 * Given bytes representing a FIDL message, MessageHeader will provide txid and ordinal values.
 */
class MessageHeader {
  /**
   * @param {ArrayBuffer} bytes The bytes of the message.
   */
  constructor(bytes) {
    this.buf = bytes;
    this.view = new DataView(this.buf);
    this.txid = this.view.getUint32(0, true);
    this.ordinal = this.view.getBigUint64(8, true);
  }
};

/**
 * Represents the client side of a protocol.
 *
 * When you construct an object of this type, methods on the protocol will
 * automatically be populated from the protocol definition.  You can then call
 * them.
 */
class ProtocolClient {
  /**
   * Constructor for ProtocolClient.
   * @param {channel} A zx.Channel object OR an existing protocol to duplicate
   * @param {protocol} A string or a fidling object representing the protocol.
   */
  constructor(channel, protocol) {
    // Not sure if protocolName is a string or a fidling.
    if (typeof protocol === 'string' || protocol instanceof String) {
      this._protocol_name = protocol;
      let [library_name, _] = protocol.split('/');
      this._decl = loadLibrary(library_name).lookupProtocol(protocol);
    } else {
      this._protocol_name = protocol.name;
      this._decl = protocol;
    }

    // Not sure if we are being passed a channel or an existing protocol client to dup.
    if (channel instanceof zx.Channel) {
      this._impl = new ProtocolClientImpl(this._decl, channel);
    } else if ('_impl' in channel) {
      this._impl = channel._impl;
    } else {
      throw 'Illegal argument passed as channel to ProtocolClient constructor';
    }

    // Populate the ProtocolClient object with methods corresponding to the protocol methods.
    const completions = [];
    for (const method of this._decl.methods) {
      let definedProperty = false;
      if (method.has_request) {
        definedProperty = true;
        this[method.name] = (...args) => this._impl._invokeMethod(method, args);
        Object.defineProperty(this[method.name], 'length', {value: method.maybe_request.length});
      } else if (method.has_response) {
        definedProperty = true;
        this[method.name] = (fn) => this._impl._setEventProcessor(method, fn);
      }
      if (definedProperty) {
        let help = methodHelp(method);
        if (help) {
          Object.defineProperty(this[method.name], Symbol.for('help'), {value: help});
        }
        completions.push(method.name);
      }
    }
    this[Symbol.for('completions')] = completions;
  }

  /**
   * Closes the given ProtocolClient instance.
   */
  static close(protocolClient) {
    protocolClient._impl._close();
  }
};

// Something fidl_codec should do: when we improve the JSON output for it, this should be
// less necessary.
function parseFidlcatHandle(handleStr) {
  var regex = /[A-Z_]*:([0-9a-f]+)\(.*\)/gi;
  return parseInt(handleStr.replace(regex, '$1'), 16);
}

function convertResponseHandles(methodDecl, message) {
  for (let i = 0; i < methodDecl.maybe_response.length; i++) {
    if (methodDecl.maybe_response[i].type.kind == 'identifier') {
      // TODO: Look up identifier and recurse.
      continue;
    }
    if (methodDecl.maybe_response[i].type.kind == 'request') {
      const handleValue = parseFidlcatHandle(message[methodDecl.maybe_response[i].name]);
      message[methodDecl.maybe_response[i].name] = zx.Channel.fromValueDeprecated(handleValue);
    }
    if (methodDecl.maybe_response[i].type.kind == 'handle') {
      const handleValue = parseFidlcatHandle(message[methodDecl.maybe_response[i].name]);
      switch (methodDecl.maybe_response[i].type.subtype) {
        case 'job':
          message[methodDecl.maybe_response[i].name] = zx.Job.fromValueDeprecated(handleValue);
          break;
        case 'handle':
        case 'bti':
        case 'channel':
        case 'debuglog':
        case 'event':
        case 'eventpair':
        case 'exception':
        case 'fifo':
        case 'guest':
        case 'interrupt':
        case 'iommu':
        case 'pager':
        case 'pcidevice':
        case 'pmt':
        case 'port':
        case 'process':
        case 'profile':
        case 'resource':
        case 'socket':
        case 'suspendtoken':
        case 'thread':
        case 'timer':
        case 'vcpu':
        case 'vmar':
        case 'vmo':
          throw 'Unsupported handle type ' + methodDecl.maybe_response[i].type.subtype;
        default:
          throw 'Unknown handle type ' + methodDecl.maybe_response[i].type.subtype;
      }
    }
  }
  return message;
}

/**
 * The implementation class for the ProtocolClient.
 *
 * This prevents the messy implementation methods from being exposed to the
 * user.  Can be shared among multiple ProtocolClient objects (this is useful
 * for protocol composition, where you might have multiple protocols exposed on
 * the same channel).
 */
class ProtocolClientImpl {
  /**
   * @param {declaration} A fidling definition for the protocol.
   * @param {channel} A zx.Channel object for communication.
   */
  constructor(declaration, channel) {
    this._decl = declaration;
    this._channel = channel;
    this._eventProcessors = new Map();  // String representing ordinal => callback on that event.
    this._txid = 1;
    this._pending = new Map();
  }
  /**
   * Returns the next transaction id for communications over this channel.
   */
  _nextTxId() {
    const txid = this._txid;
    this._txid++;
    return txid;
  }

  /**
   * Tells this client what to do when a given event is triggered.
   * @param {method} The part of the fidling describing the event.
   * @param {fn} The function to be invoked when the event happens.
   * @returns a promise that will be resolved / rejected when the event is triggered.
   */
  _setEventProcessor(method, fn) {
    this._channel.waitAsync(
        zx.ZX_CHANNEL_READABLE | zx.ZX_CHANNEL_PEER_CLOSED, () => this._readable(method));
    return new Promise((resolve, reject) => {
      if (typeof method.generated_ordinal != 'undefined') {
        this._eventProcessors.set(method.generated_ordinal, (args) => {
          try {
            resolve(fn(args));
          } catch (e) {
            reject(e);
            console.log(e);
          }
        });
      }
      this._eventProcessors.set(method.ordinal, (args) => {
        try {
          resolve(fn(args));
        } catch (e) {
          reject(e);
          console.log(e);
        }
      });
    });
  }

  /**
   * The implementation of method invocation.  The ProtocolClient methods are a
   * thin decorator around this method.  If the method takes a response, it will
   * set up an event handler to deal with the response.
   */
  _invokeMethod(method, args) {
    return new Promise((resolve, reject) => {
      try {
        let txid = 0;
        if (method.has_response) {
          this._channel.waitAsync(
              zx.ZX_CHANNEL_READABLE | zx.ZX_CHANNEL_PEER_CLOSED, () => this._readable(method));
          txid = this._nextTxId();
        }
        const encoder = new MessageEncoder(txid, method.ordinal);
        encoder.encodeArgs(method.maybe_request, method.maybe_request_size, args);
        if (method.has_response) {
          this._pending.set(txid, [method, resolve, reject]);
        }
        this._channel.write(encoder.bytes, encoder.handles);
        if (!method.has_response) {
          resolve();
        }  // otherwise, gets resolved when we invoke _readable.
      } catch (e) {
        console.log(e);
        console.log(e.stack);
        reject(e);
      }
    });
  }

  /**
   * Handles the response from a protocol method.  It looks up what methods
   * we're expecting a response from and tries to correlate it with the txid
   * received.
   * @param method is the FIDL IR for the method.
   */
  _readable(method) {
    const [bytes, handles] = this._channel.read();
    const header = new MessageHeader(bytes);

    let resolve = this._eventProcessors.get(header.ordinal.toString());
    if (typeof resolve == 'undefined') {
      // It's a response to a request.
      const txid = header.txid;
      if (!this._pending.has(txid)) {
        console.log(`Unexpected transaction id ${txid} from ${this._decl.name}`);
        return;
      }
      const [meth, resolve_, reject] = this._pending.get(txid);
      resolve = resolve_;
      this._pending.delete(txid);
    }
    if (typeof resolve == 'undefined') {
      throw 'Unexpected message with ordinal ' + header.ordinal + ' received';
    }
    // TODO(jeremymanson): Make sure strings in decoded response are escaped properly
    const responseString = internalLibrary.decodeResponse(bytes, handles);
    const response = convertResponseHandles(method, JSON.parse(responseString));
    resolve(response);
  }

  _close() {
    this._channel.close();
  }
}

/**
 * Enough of a message encoder to tide us over until fidl_codec supports message encoding.
 * TODO(jeremymanson): Delete me!
 */
class MessageEncoder {
  constructor(txid, ordinal) {
    this.txid = txid;
    this.buf = new ArrayBuffer(MSG_MAX_BYTES);
    this.view = new DataView(this.buf);
    this.length = 0;
    this.handles = [];
    this.offset = 0;
    // encode the header
    this.view.setUint32(0, txid, true);
    this.view.setUint32(4, 0, true);  // uint8_t flags[3], plus one more which we overwrite:
    this.view.setUint32(7, 1, true);  // The current magic number.
    this.view.setBigUint64(8, BigInt(ordinal), true);
  }
  encodeArgs(args, size, values) {
    this.length = size;
    this.offset = 16;
    let typelist = Array.from(args);
    let valuelist = Array.from(values);
    typelist.push({'type': {'kind': 'pad_to_boundary'}, 'size': '-1'});
    valuelist.push(null);
    for (let i = 0; i < typelist.length; i++) {
      const arg = typelist[i];
      const value = valuelist[i];
      const val = this.encode(arg.type, this.offset, arg.size, value);
      this.offset = val.offset
      for (let i = 0; i < val.more.length; i++) {
        typelist.push(val.more[i].arg);
        valuelist.push(val.more[i].value);
      }
    }
    this.length = this.offset;
  }
  encode(type, offset, size, value) {
    let val = {'offset': offset, 'more': []};
    switch (type.kind) {
      case 'primitive':
        switch (type.subtype) {
          case 'bool':
            this.view.setUint8(offset, value ? 1 : 0);
            val.offset = offset + 1;
            break;
          case 'int8':
            this.view.setInt8(offset, value);
            val.offset = offset + 1;
            break;
          case 'uint8':
            this.view.setUint8(offset, value);
            val.offset = offset + 1;
            break;
          case 'int16':
            this.view.setInt16(offset, value, true);
            val.offset = offset + 2;
            break;
          case 'uint16':
            this.view.setUint16(offset, value, true);
            val.offset = offset + 2;
            break;
          case 'int32':
            this.view.setInt32(offset, value, true);
            val.offset = offset + 4;
            break;
          case 'uint32':
            this.view.setUint32(offset, value, true);
            val.offset = offset + 4;
            break;
          case 'int64':
            this.view.setBigInt64(offset, BigInt(value), true);
            val.offset = offset + 8;
            break;
          case 'uint64':
            this.view.setBigUint64(offset, BigInt(value), true);
            val.offset = offset + 8;
            break;
          case 'float32':
            this.view.setFloat32(offset, value, true);
            val.offset = offset + 4;
            break;
          case 'float64':
            this.view.setFloat64(offset, value, true);
            val.offset = offset + 8;
            break;
          default:
            throw new Error('Don\'t know how to encode primitive: ' + JSON.stringify(type));
            break;
        }
        break;
      case 'string':
        // convert to byte array of utf-8
        var array = MessageEncoder._stringToUtf8ByteArray(value);
        this.view.setBigUint64(offset, BigInt(array.length), true);
        offset += 8;
        for (var i = 0; i < 8; i++) {
          this.view.setUint8(offset + i, 0xff, true);
        }
        val.offset = offset + 8
        val.more = [{'arg': {'type': {'kind': 'string_contents'}, 'size': '-1'}, 'value': array}];
        break;
      case 'string_contents':
        for (var i = 0; i < value.length; i++) {
          this.view.setUint8(val.offset++, value[i], true);
        }
        val.more = [{'arg': {'type': {'kind': 'pad_to_boundary'}, 'size': '-1'}, 'value': null}];
        break;
      case 'request':
        // FIDL_HANDLE_ABSENT = 0
        // FIDL_HANDLE_PRESENT = UINT32_MAX
        let handleIndicator = (value == null) ? 0 : 0xFFFFFFFF;
        this.view.setUint32(val.offset, handleIndicator, true);
        val.offset += 4;
        if (value != null) {
          this.handles.push(value);
        }
        break;
      case 'pad_to_boundary':
        // Pad the end of the structure to an 8-byte boundary.
        while (val.offset % 8 != 0) {
          this.view.setUint8(val.offset++, 0, true);
        }
        break;
      default:
        throw new Error('Don\'t know how to encode: ' + JSON.stringify(type));
    }
    return val;
  }
  get bytes() {
    return this.buf.slice(0, this.length);
  }

  static _stringToUtf8ByteArray(str) {
    let out = [];
    for (let i = 0; i < str.length; i++) {
      let ch = str.charCodeAt(i);
      if (ch < 0x80) {
        out.push(ch);
      } else if (ch < 0x800) {
        out.push((ch >> 6) | 0xC0, (ch & 63) | 0x80);
      } else if (
          ((ch & 0xFC00) == 0xD800) && (i + 1) < str.length &&
          ((str.charCodeAt(i + 1) & 0xFC00) == 0xDC00)) {
        // Surrogate Pair
        ch = 0x10000 + ((ch & 0x03FF) << 10) + (str.charCodeAt(++i) & 0x03FF);
        out.push(
            (ch >> 18) | 0xF0, ((ch >> 12) & 0x3F) | 0x80, ((ch >> 6) & 0x3F) | 0x80,
            (ch & 63) | 0x80);
      } else {
        out.push((ch >> 12) | 0xE0, ((ch >> 6) & 0x3F) | 0x80, (ch & 0x3F) | 0x80);
      }
    }
    return out;
  }
};

/**
 * A convenience class that allows a user to say something like:
 *
 * client = new Request(fidling.fuchsia.io.Directory).getProtocolClient();
 *
 * And use client appropriately.
 */
class Request {
  constructor(protocolIr) {
    this._protocolIr = protocolIr;
    const channels = zx.Channel.create();
    this._serverEndpoint = channels[1];
    this._clientEndpoint = channels[0];
  }

  /**
   * Generates a new ProtocolClient for this request
   */
  getProtocolClient() {
    return new fidl.ProtocolClient(this._clientEndpoint, this._protocolIr.name);
  }

  /**
   * Returns a zx.Channel representing the server endpoint for this request.
   */
  getChannelForServer() {
    return this._serverEndpoint;
  }
};

/**
 * Returns a ProtocolClient that connects to the server at the given path.  For example:
 * client = connectToServiceAt('/svc/fuchsia.power.BatteryManager', 'fuchsia.power/BatteryManager');
 */
function connectToServiceAt(path, protocolName) {
  const channel = new zx.Channel(fdio.serviceConnect(path));
  return new ProtocolClient(channel, protocolName);
}


/**
 * Connects to a service exposed via /svc.  For example:
 * client = connectToService('fuchsia.power.BatteryManager');
 */
function connectToService(serviceName) {
  const idx = serviceName.lastIndexOf('.');
  const name = serviceName.substr(idx + 1);
  const libraryName = serviceName.substr(0, idx);
  return connectToServiceAt(`/svc/${serviceName}`, `${libraryName}/${name}`);
}

global['fidl'] = {
  connectToService,
  loadLibrary,
  loadLibraryIr,
  ProtocolClient,
  Request,
};

function constToValue(c) {
  if (c.type.kind == 'primitive') {
    switch (c.type.subtype) {
      case 'int8':
      case 'uint8':
      case 'int16':
      case 'uint16':
      case 'int32':
      case 'uint32':
      case 'int64':
      case 'uint64':
        return parseInt(c.value.literal.value);
      case 'float32':
      case 'float64':
        return parseFloat(c.value.literal.value);
      default:
        throw 'Unknown primitive type ' + JSON.stringify(c.type) + ' ' + c.type.kind + ' ' +
            c.type.subtype;
    }
  }
}

// Make fidl definitions available on an object called `fidling`
// Asking for fidling.libraryName.CONST returns a const value.
// Asking for fidling.libraryName.Protocol returns the IR for the protocol.
var fidling_handler = {
  get: function(obj, prop) {
    let ret = undefined;
    libraries.forEach(function(val, library, map) {
      // mangle library names to be valid JS identifiers
      const libraryName = library.replace(/\./g, '_');
      if (prop == libraryName) {
        ret = new Proxy({_name: library}, {
          get: function(library_obj, lib_prop) {
            const maybeName = library_obj._name + '/' + lib_prop;
            for (let i = 0; i < val.ir.const_declarations.length; i++) {
              let c = val.ir.const_declarations[i];
              if (c.name == maybeName) {
                return constToValue(c);
              }
            }
            for (let i = 0; i < val.ir.interface_declarations.length; i++) {
              let p = val.ir.interface_declarations[i];
              if (p.name == maybeName) {
                return p;
              }
            }
            // And similarly for non-const decls
            return undefined;
          }
        });
      }
    });
    return ret;
  }
};
const fidling = new Proxy({}, fidling_handler);

global['fidling'] = fidling;
})(globalThis);
