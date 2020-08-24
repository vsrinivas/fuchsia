// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// @dart = 2.8

import 'dart:core';

import '../render/ast.dart';
import '../types.dart';
import 'index.dart';

enum SourceLang { cpp, rust, go }

extension on SourceLang {
  String toUnqualifiedName() =>
      toString().substring(runtimeType.toString().length + 1);
}

class SourceLangContextMixin {
  // ignore: use_setters_to_change_properties
  void initSourceLangContextMixin(Report report) {
    _report = report;
  }

  Report _report;

  SourceLang get lang => _lang(this);

  final _lang = Lazy<SourceLang, ProgramContext>((ProgramContext self) {
    final result = _fuzzyDetectLang(self._report);
    if (result == null) {
      print('Warning: ${self.name} unknown language');
      _debugDumpReport(self._report);
      throw Exception('Unable to detect language for ${self.name}');
    }
    return result;
  });

  static void _debugDumpReport(Report report) {
    for (final compileUnit in report.compileUnits) {
      print(compileUnit.name);
      for (final symbol in compileUnit.symbols) {
        print('  $symbol');
      }
    }
  }
}

class SourceLangCompileContextMixin {
  bool get isRust => _isRust(this);

  final _isRust = Lazy<bool, CompileUnitContext>((CompileUnitContext self) =>
      self.name.endsWith('.rs') || self.name.startsWith('[crate: '));

  bool get isCFamilyFileExtension => _isCFamilyFileExtension(this);

  static final _cFamilyFileExtensionRegex = RegExp(r'\.(c|cc|cpp|h|hh|hpp)$');

  final _isCFamilyFileExtension = Lazy<bool, CompileUnitContext>(
      (CompileUnitContext self) =>
          _cFamilyFileExtensionRegex.hasMatch(self.name));
}

class SourceLangQuery extends Query
    implements QueryReport, IgnorePageInHeatmapFilter {
  static const String description =
      'Groups binaries based on the programming language they are '
      'predominantly written in.';

  @override
  String getDescription() => description;

  @override
  void addReport(Report report) {
    _tallies[report.context.lang]
      ..size += report.fileTotal
      ..count += 1;
  }

  @override
  void mergeWith(Iterable<Query> others) {
    for (final other in others) {
      if (other is SourceLangQuery) {
        for (final entry in other._tallies.entries) {
          _tallies[entry.key] += entry.value;
        }
      } else {
        throw Exception('$other must be $runtimeType');
      }
    }
  }

  @override
  QueryReport distill() => this;

  final _tallies = Map<SourceLang, Tally>.fromEntries(
      SourceLang.values.map((s) => MapEntry(s, Tally.zero())));

  @override
  String toString() => printMapSorted(_tallies);

  @override
  Iterable<AnyNode> export() {
    final sortedBySize = _tallies.keys.toList()
      ..sort((a, b) => -_tallies[a].size.compareTo(_tallies[b].size));
    return sortedBySize.map((k) => Node(
            title: StyledString([
          AddColor.white(Plain(k.toUnqualifiedName())),
          Plain(': ${_tallies[k]}'),
        ])));
  }
}

SourceLang _fuzzyDetectLang(Report report) {
  const goKeyLibraries = <String>{
    'net/http',
    'crypto/tls',
    'github.com/golang/protobuf/proto',
    'netstack',
    'gvisor.dev/gvisor/pkg/tcpip/transport/tcp',
    'math/big',
    'gvisor.dev/gvisor/pkg/tcpip/stack',
    'reflect',
    'vendor/golang.org/x/text/unicode/norm',
    'fuchsia.googlesource.com/pmd/pkgfs',
    'encoding/json',
    'gvisor.dev/gvisor/pkg/state',
    'syscall/zx/fidl',
    'regexp/syntax',
    'vendor/golang.org/x/net/dns/dnsmessage',
    'crypto/x509',
    'netstack/filter',
    'syscall/zx/io',
    'golang.org/x/net/dns/dnsmessage',
    'crypto/ed25519/internal/edwards25519',
    'strconv',
    'regexp',
    'vendor/golang.org/x/net/idna',
    'encoding/asn1',
    'fidl/fuchsia/posix/socket',
    'fidl/fuchsia/io',
    'crypto/elliptic',
    'compress/flate',
    'fmt',
    'runtime/pprof',
    'syscall/zx/fdio',
    'encoding/hex',
    'crypto/rand',
    'gvisor.dev/gvisor/pkg/tcpip/link/loopback',
    'fuchsia.googlesource.com/pmd/blobfs',
    'gvisor.dev/gvisor/pkg/tcpip/network/hash',
    'fidl/fuchsia/storage/metrics',
    'crypto/dsa',
    'crypto/rc4',
    'container/heap',
    'gvisor.dev/gvisor/pkg/tcpip/seqnum',
    'fuchsia.googlesource.com/pmd/allowlist',
    'amber/urlscope',
    'hash/fnv',
    'netstack/link',
    'internal/testlog',
    'runtime/internal/sys',
    'vendor/golang.org/x/text/transform',
    'fidl/fuchsia/mem',
    'syscall/zx/mem',
    'internal/oserror',
    'sync/atomic',
    'unicode/utf16',
    'crypto/internal/randutil',
    'netstack/util',
    'fuchsia.googlesource.com/pmd/iou',
    'math/bits',
    'runtime/cgo',
    'crypto/subtle',
    'net/http/httptrace',
    'vendor/golang.org/x/crypto/cryptobyte/asn1',
    'runtime/internal/atomic',
    'gvisor.dev/gvisor/pkg/rand',
    'gvisor.dev/gvisor/pkg/tcpip/hash/jenkins',
    'gvisor.dev/gvisor/pkg/linewriter',
  };
  var numCppUnits = 0;
  var numRustUnits = 0;
  var numGoUnits = 0;
  for (var compileUnit in report.compileUnits) {
    if (compileUnit.context.isCFamilyFileExtension) {
      numCppUnits += 1;
    } else if (compileUnit.context.isRust) {
      numRustUnits += 1;
    } else if (goKeyLibraries.contains(compileUnit.name)) {
      numGoUnits += 1;
    }
  }
  // Preferentially select rust in case multiple languages mix.
  if (numRustUnits > 0 &&
      numRustUnits >= numGoUnits &&
      // Account for Rust binaries linking in the C++ libunwind library.
      numCppUnits < numRustUnits * 2) {
    return SourceLang.rust;
  }
  if (numGoUnits >= numCppUnits &&
      numGoUnits >= numRustUnits &&
      numGoUnits > 0) {
    return SourceLang.go;
  }
  if (numCppUnits >= numRustUnits &&
      numCppUnits >= numGoUnits &&
      numCppUnits > 0) {
    return SourceLang.cpp;
  }

  return null;
}
