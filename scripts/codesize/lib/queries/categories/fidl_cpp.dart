// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// @dart = 2.8

import 'dart:core';

import '../code_category.dart';
import '../index.dart';

bool _compileUnitHas(CompileUnitContext compileUnit, List<RegExp> regexp) {
  if (!compileUnit.isCFamilyFileExtension) return false;
  if (matchRegexEnsureAtMostOne(compileUnit.name, regexp)) return true;
  return false;
}

/// As defined in FIDL spec.
const _identifier = r'[a-zA-Z]([a-zA-Z0-9_]*[a-zA-Z0-9])?';

class CppCodingTableContextMixin {
  /// All compile unit name (file name) regexes for C++ FIDL coding tables.
  static final List<RegExp> _codingTable = [
    // Examples:
    // fidling/gen/sdk/fidl/fuchsia.feedback/fuchsia.feedback.fidl.tables.c
    // fidling/gen/zircon/system/fidl/fuchsia-mem/fuchsia-mem.fidl.tables.c
    RegExp(r'^fidling/gen/.*/fidl/.*\.tables\.c$'),

    // gen/build/fuchsia/fidl/fuchsia/ui/gfx/cpp/tables.c
    RegExp(r'fuchsia/fidl/.*/cpp/tables\.c$'),

    // gen/third_party/fuchsia-sdk/sdk/fidl/fuchsia.io/fidl/fuchsia.io.fidl-tables.c
    RegExp(r'fuchsia-sdk/sdk/fidl/.*/fidl/.*\.fidl-tables\.c$'),

    // fidling/gen/sdk/fidl/fuchsia.feedback/fuchsia.feedback.fidl.tables.c
    RegExp(r'^gen/system/fidl/.*\.tables/tables\.c$'),

    // gen/fuchsia/fidl/some.custom.lib.fidl-tables.c
    // gen/foo/bar/fuchsia/fidl/some.custom.lib/fidl/some.custom.lib.fidl-tables.c
    RegExp(r'^gen/?.*/fuchsia/fidl/.*\.fidl-tables\.c$'),

    // .../.../fuchsia/sdk/fidl/fuchsia_io/fuchsia_io_tables.c
    RegExp(r'/fuchsia/sdk/fidl/.*/[a-z0-9_]+_tables\.c$'),

    // ../../fuchsia/fidl_tables.c
    RegExp(r'/fuchsia/fidl_tables\.c$'),
  ];

  /// Returns if the name of the compilation unit suggests it came from
  /// the FIDL coding tables generator. Usually it's some variation of
  /// `foo/bar/tables.c`.
  bool get isNameCppCodingTables => _isNameCppCodingTables(this);

  final _isNameCppCodingTables = Lazy<bool, CompileUnitContext>(
      (CompileUnitContext self) => _compileUnitHas(self, _codingTable));

  final _knownLibrariesByUnderscore = <String>{};

  /// Compiles all elements in `_knownLibrariesByUnderscore` into one mega regex,
  /// e.g. `{'fuchsia_io', 'fuchsia_mem'} => '(fuchsia_io|fuchsia_mem)_'`
  RegExp get _regexFromKnownLibrariesByUnderscore =>
      __regexFromKnownLibrariesByUnderscore(this);

  final __regexFromKnownLibrariesByUnderscore =
      Lazy<RegExp, CompileUnitContext>((CompileUnitContext self) {
    if (self._knownLibrariesByUnderscore.isEmpty) return null;
    return RegExp('(${self._knownLibrariesByUnderscore.join('|')})_');
  });
}

class CppCodingTableCategory extends CodeCategory implements SomeFidlCategory {
  const CppCodingTableCategory();

  @override
  String get description =>
      'FIDL coding tables describing how to serialize a FIDL message, '
      'shared by all C/C++ family of bindings';

  // v1_Vector4294967295nonnullable5uint8Table.6287
  // String4294967295nullableTable
  static final _collectionRegex =
      RegExp(r'^(v1_)?(Vector|String)\d+(non)?nullable.*Table(\.\d+)?$');

  // Array384_31fuchsia_perfmon_cpu_EventConfigTable
  static final _arrayRegex =
      RegExp(r'^(v1_)?Array\d+_\d+[a-z].*Table(\.\d+)?$');

  // HandleresourcenonnullableTable
  // HandlefifononnullableTable.669
  static final _handleRegex =
      RegExp(r'^(v1_)Handle[a-z]+nullableTable(\.\d+)?$');

  // EnumValidatorFor_fuchsia_auth_Status
  static final _enumValidator =
      RegExp('^EnumValidatorFor_(v1_)?($_identifier)_([a-zA-Z0-9]+)\$');

  // Fields56fuchsia_tracing_provider_RegistryRegisterProviderRequest
  // Fields59v1_fuchsia_tracing_provider_RegistryRegisterProviderRequest
  static final _fieldsRegex = RegExp(
      '^Fields(\\d)+(v1_)?($_identifier)_([a-zA-Z0-9]+)(Request|Response|_Result)\$');

  // Protocol39fuchsia_ui_input_PointerCaptureListenernonnullableTable
  static final _protocolRegex = RegExp(
      '^(v1_)?Protocol(\\d)+(v1_)?($_identifier)_([a-zA-Z0-9]+)nullableTable(\\.\d+)?\$');

  @override
  bool match(
      String symbol, CompileUnitContext compileUnit, ProgramContext program) {
    if (compileUnit.isNameCppCodingTables) return true;

    // Check symbol names for coding table signatures
    if (_handleRegex.hasMatch(symbol)) return true;
    if (_collectionRegex.hasMatch(symbol)) return true;
    if (_arrayRegex.hasMatch(symbol)) return true;

    var match = _enumValidator.firstMatch(symbol);
    if (match != null) {
      compileUnit._knownLibrariesByUnderscore.add(match.group(2));
      return true;
    }

    match = _fieldsRegex.firstMatch(symbol);
    if (match != null) {
      compileUnit._knownLibrariesByUnderscore.add(match.group(3));
      return true;
    }

    match = _protocolRegex.firstMatch(symbol);
    if (match != null) {
      compileUnit._knownLibrariesByUnderscore.add(match.group(4));
      return true;
    }

    return false;
  }

  static final _looseFieldsRegex = RegExp(r'^Fields(\d)+(v1_)?(.*)$');

  static final _tableEnding = RegExp(r'Table(.\d+)?$');

  @override
  bool rematch(
      String symbol, CompileUnitContext compileUnit, ProgramContext program) {
    if (compileUnit._regexFromKnownLibrariesByUnderscore == null) return false;

    final match = _looseFieldsRegex.firstMatch(symbol);
    if (match != null) {
      final tail = match.group(3);
      if (compileUnit._regexFromKnownLibrariesByUnderscore.hasMatch(tail))
        return true;
    }

    if (_tableEnding.hasMatch(symbol)) {
      if (compileUnit._regexFromKnownLibrariesByUnderscore.hasMatch(symbol))
        return true;
    }

    return false;
  }
}

class CppDomainObjectContextMixin {
  /// All compile unit name (file name) regexes for High-Level C++ FIDL
  /// generated server/client code.
  static final List<RegExp> _hlcppDomainObject = [
    // fidling/gen/sdk/fidl/fuchsia.feedback/fuchsia/feedback/cpp/fidl.cc
    // fidling/gen/src/connectivity/bluetooth/fidl/fuchsia/bluetooth/host/cpp/fidl.cc
    RegExp(r'^fidling/gen/.*/fidl/.*/cpp/fidl\.cc$'),

    // gen/third_party/fuchsia-sdk/sdk/fidl/fuchsia.modular/fidl/fuchsia/modular/cpp/fidl.cc
    // gen/build/fuchsia/fidl/fuchsia/accessibility/semantics/cpp/fidl.cc
    // gen/foo/internal/fuchsia/fidl/some.lib/fidl/some/lib/cpp/fidl.cc
    RegExp(r'^gen/[a-z_-]+/.*/fidl/.*/cpp/fidl\.cc$'),

    // .../.../fuchsia/sdk/fidl/fuchsia_io/fuchsia_io_cc_codegen.cc/fuchsia/io/cpp/fidl.cc
    RegExp(r'/fuchsia/sdk/fidl/.*/cpp/fidl\.cc$'),

    // .../genfiles/.../.../.../.../fuchsia/fidl_cc_codegen.cc/google/duocore/cpp/fidl.cc
    RegExp(r'/genfiles/.*/fuchsia/fidl_cc_codegen\.cc/.*/cpp/fidl\.cc$'),
  ];

  bool get isNameHlcppDomainObject => _isNameHlcppDomainObject(this);

  final _isNameHlcppDomainObject = Lazy<bool, CompileUnitContext>(
      (CompileUnitContext self) => _compileUnitHas(self, _hlcppDomainObject));

  /// All compile unit name (file name) regexes for Low-Level C++ FIDL
  /// generated server/client code.
  static final List<RegExp> _llcppDomainObject = [
    // fidling/gen/zircon/system/fidl/fuchsia-kernel/fuchsia/kernel/llcpp/fidl.cc
    // fidling/gen/sdk/fidl/fuchsia.blobfs/fuchsia/blobfs/llcpp/fidl.cc
    RegExp(r'^fidling/gen/.*/fidl/.*/llcpp/fidl\.cc$'),

    // gen/system/fidl/fuchsia-io/fuchsia-io.json/fidl.cc
    RegExp(r'^gen/.*/fidl/.*\.json/fidl\.cc$'),
  ];

  bool get isNameLlcppDomainObject => _isNameLlcppDomainObject(this);

  final _isNameLlcppDomainObject = Lazy<bool, CompileUnitContext>(
      (CompileUnitContext self) => _compileUnitHas(self, _llcppDomainObject));

  /// In the first pass (HlcppDomainObjectCategory.match), we would extract the
  /// set of observed FIDL library namespaces and store them here. This allows
  /// us to detect tricky symbol names with much higher confidence in the second
  /// pass (HlcppDomainObjectCategory.rematch).
  final _knownLibraryNamespaces = <String>{};

  /// Compiles all elements in `_knownLibraryNamespaces` into one mega regex,
  /// e.g. `{'fuchsia::io', 'fuchsia::mem'} => '^(fuchsia::io|fuchsia::mem)'`
  RegExp get _regexFromKnownLibraryNamespaces =>
      __regexFromKnownLibraryNamespaces(this);

  final __regexFromKnownLibraryNamespaces =
      Lazy<RegExp, CompileUnitContext>((CompileUnitContext self) {
    if (self._knownLibraryNamespaces.isEmpty) return null;
    return RegExp(
        '^(non-virtual thunk to )?(${self._knownLibraryNamespaces.join('|')})');
  });
}

class HlcppDomainObjectCategory extends CodeCategory
    implements SomeFidlCategory {
  const HlcppDomainObjectCategory();

  @override
  String get description =>
      'Generated server and client bindings code from the '
      'high-level FIDL C++ bindings';

  static const _nsAndClass = r'(([a-z0-9]+::)+)[a-zA-Z0-9_]+';

  static const _possibilitiesAfterNsAndClass = [
    // fuchsia::io::Directory_Proxy::Directory_Proxy(fidl::internal::ProxyController*)
    r'_Proxy::[a-zA-Z0-9_]+_Proxy\(fidl::internal::ProxyController\*\)$',

    // fuchsia::io::Directory_Stub::Dispatch_(fidl::Message, fidl::internal::PendingResponse)
    r'_Stub::Dispatch_\(fidl::Message, fidl::internal::PendingResponse\)$',

    // fuchsia::io::Directory_Proxy::Dispatch_(fidl::Message)
    r'_Proxy::Dispatch_\(fidl::Message\)$',

    // fuchsia::io::NodeInfo::Encode(fidl::Encoder*, unsigned long)
    r'::Encode\(fidl::Encoder\*, unsigned long\)$',

    // fuchsia::io::NodeInfo::Decode(fidl::Decoder*, fuchsia::io::NodeInfo*, unsigned long)
    r'::Decode\(fidl::Decoder\*.*unsigned long\)$',

    // fuchsia::io::NodeInfo::EnsureStorageInitialized(unsigned long)
    r'::EnsureStorageInitialized\(unsigned long\)$',

    // fuchsia::io::Directory_RequestEncoder::Open(fidl::Encoder*, foo)
    '_RequestEncoder::$_identifier\\(fidl::Encoder\\*',

    // fuchsia::io::Directory_RequestDecoder::GetType(unsigned long, bool*)
    '_RequestDecoder::GetType\\(unsigned long',
  ];

  static final _nsAndClassPattern = RegExp(r'^(non-virtual thunk to )?'
      r'(fidl::Clone\()?'
      '$_nsAndClass(${_possibilitiesAfterNsAndClass.join("|")})');

  static final _capturingRegexes = [
    _nsAndClassPattern,

    // int fidl::internal::SingleUseMessageHandler::InvokeImpl<
    //     fuchsia::io::(anonymous namespace)::Directory_NodeGetFlags_ResponseHandler(fit::function_impl<16ul, false, void (int, unsigned int)>)::$_28
    // >(fidl::internal::SingleUseMessageHandler*, fidl::Message&&)
    RegExp(r'^int fidl::internal::SingleUseMessageHandler::InvokeImpl<'
        r'(([a-z0-9]+::)+)\(anonymous namespace\)::'
        '${_identifier}_ResponseHandler\\(fit::function'),

    // fuchsia::io::(anonymous namespace)::Directory_Link_ResponseHandler::OnMessage(fidl::Message)
    RegExp('^(([a-z0-9]+::)+)\\(anonymous namespace\\)::'
        '${_identifier}_ResponseHandler::OnMessage\\(fidl::Message'),
  ];

  static final _codingTraits = RegExp('^(void )?fidl::CodingTraits<');

  @override
  bool match(
      String symbol, CompileUnitContext compileUnit, ProgramContext program) {
    if (compileUnit.isNameHlcppDomainObject) {
      // Some parts of HLCPP runtime are imported here.
      if (HlcppRuntimeCategory._fidlRegex.hasMatch(symbol)) {
        return false;
      } else {
        return true;
      }
    }

    // Check symbol names for HLCPP domain object signatures. If we are able to
    // extract the library name, also add it to the set of known library names.
    RegExpMatch match;

    // fidl::CodingTraits<
    //     std::__1::unique_ptr<
    //         fuchsia::io::NodeInfo,
    //         std::__1::default_delete<fuchsia::io::NodeInfo>
    //     >,
    //     void
    // >::Decode(fidl::Decoder*, ...)
    if (symbol.startsWith(_codingTraits)) return true;

    for (final regex in _capturingRegexes) {
      if ((match = regex.firstMatch(symbol)) != null) {
        final nsPrefix = match.pattern == _nsAndClassPattern
            ? match.group(3)
            : match.group(1);
        if (!nsPrefix.endsWith('::'))
          throw Exception('Namespace prefix should end with ::, got $nsPrefix');
        final libraryNamespace = nsPrefix.substring(0, nsPrefix.length - 2);
        if (libraryNamespace != 'fidl' && libraryNamespace != 'llcpp')
          compileUnit._knownLibraryNamespaces.add(libraryNamespace);
        return true;
      }
    }

    return false;
  }

  @override
  bool rematch(
      String symbol, CompileUnitContext compileUnit, ProgramContext program) {
    if (compileUnit._regexFromKnownLibraryNamespaces == null) return false;
    final match =
        compileUnit._regexFromKnownLibraryNamespaces.firstMatch(symbol);
    if (match == null) return false;
    final symbolAfterLibrary =
        symbol.substring(match.group(1)?.length ?? 0 + match.group(2).length);

    // [fuchsia::io]::Node_Stub::FooBar(...)
    // [fuchsia::io]::Directory_Proxy::FooBar(...)
    // [fuchsia::io]::(anonymous namespace)::Directory_Link_ResponseHandler::~Directory_Link_ResponseHandler()
    // [fuchsia::io]::Vmofile::operator=(fuchsia::io::Vmofile&&)
    // [fuchsia::io]::Node::Name_
    // [fuchsia::io]::NodeInfo::Destroy()
    // fidl::Clone([fuchsia::io]::NodeInfo const&, fuchsia::io::NodeInfo*))
    if (symbolAfterLibrary.startsWith('::') && symbolAfterLibrary.length > 5)
      return true;

    return false;
  }
}

class LlcppDomainObjectCategory extends CodeCategory
    implements SomeFidlCategory {
  const LlcppDomainObjectCategory();

  @override
  String get description =>
      'Generated server and client bindings code from the '
      'low-level FIDL C++ bindings';

  static final _fidlRegex =
      RegExp(r'^fidl::(tracking_ptr|EncodeResult|DecodeResult|LinearizeResult'
          '|internal::SyncCallBase|Completer<)');
  static final _sendMessageRegex =
      RegExp(r'^(void |int )?fidl::(CompleterBase::SendReply<|Write<llcpp::)');

  @override
  bool match(
      String symbol, CompileUnitContext compileUnit, ProgramContext program) {
    if (compileUnit.isNameLlcppDomainObject) {
      // Some parts of LLCPP runtime are imported here.
      if (LlcppRuntimeCategory._fidlRegex.hasMatch(symbol)) {
        return false;
      } else {
        return true;
      }
    }

    if (symbol.startsWith('llcpp::')) return true;
    if (_fidlRegex.hasMatch(symbol)) return true;
    if (_sendMessageRegex.hasMatch(symbol)) return true;

    return false;
  }
}

class CppRuntimeContextMixin {
  static final List<RegExp> _hlcppRuntime = [
    // ../../fuchsia/sdk/linux/pkg/fidl_cpp/internal/stub_controller.cc
    // ../../fuchsia/sdk/linux/pkg/fidl_cpp_sync/internal/synchronous_proxy.cc
    // third_party/foobar/fuchsia/sdk/pkg/fidl_cpp/internal/message_reader.cc
    RegExp(
        r'^(\.\./\.\./|third_party/([a-z_-]+/)+)fuchsia/sdk/([a-z_-]+/)*pkg/fidl(_cpp|_cpp_sync)?/'),

    RegExp(
        r'^\.\./\.\./fuchsia/sdk/([a-z_-]+/)*pkg/fidl_base/(message(_buffer)?|builder)\.cc$'),

    // ../../sdk/lib/fidl/cpp/internal/proxy_controller.cc
    // ../../sdk/lib/fidl/cpp/encoder.cc
    RegExp(r'^\.\./\.\./sdk/lib/fidl/cpp'),

    // ../../third_party/fuchsia-sdk/sdk/pkg/fidl
    // ../../third_party/fuchsia-sdk/sdk/pkg/fidl_cpp
    RegExp(
        r'^\.\./\.\./third_party/fuchsia-sdk/([a-z_-]+/)*pkg/fidl(_cpp|_cpp_sync)?/'),

    RegExp(
        r'^\.\./\.\./third_party/fuchsia-sdk/([a-z_-]+/)*pkg/fidl_base/(message(_buffer)?|builder)\.cc$'),

    RegExp(
        r'^\.\./\.\./zircon/system/ulib/fidl/(message(_buffer)?|builder)\.cc$'),
  ];

  bool get isNameHlcppRuntime => _isNameHlcppRuntime(this);

  final _isNameHlcppRuntime = Lazy<bool, CompileUnitContext>(
      (CompileUnitContext self) => _compileUnitHas(self, _hlcppRuntime));

  static final List<RegExp> _llcppRuntime = [
    // ../../fuchsia/sdk/linux/pkg/fidl_base/decoding.cc
    // Note: excluding message.cc, message_buffer.cc, and builder.cc,
    // because they are HLCPP despite located in the zircon ulib/fidl folder.
    RegExp(
        r'^\.\./\.\./fuchsia/sdk/([a-z_-]+/)*pkg/fidl_base/(?!(message(_buffer)?|builder)\.cc)'),

    // ../../third_party/fuchsia-sdk/sdk/pkg/fidl_base
    RegExp(
        r'^\.\./\.\./third_party/fuchsia-sdk/([a-z_-]+/)*pkg/fidl_base/(?!(message(_buffer)?|builder)\.cc)'),

    // ../../zircon/system/ulib/fidl/linearizing.cc
    // ../../zircon/system/ulib/fidl-async/llcpp_bind.cc
    RegExp(
        r'^\.\./\.\./zircon/system/ulib/fidl(?!/(message(_buffer)?|builder)\.cc)'),
  ];

  bool get isNameLlcppRuntime => _isNameLlcppRuntime(this);

  final _isNameLlcppRuntime = Lazy<bool, CompileUnitContext>(
      (CompileUnitContext self) => _compileUnitHas(self, _llcppRuntime));
}

class HlcppRuntimeCategory extends CodeCategory implements SomeFidlCategory {
  const HlcppRuntimeCategory();

  @override
  String get description =>
      'Run-time support libraries for the high-level C++ FIDL bindings';

  static final _fidlRegex = RegExp(r'^(non-virtual thunk to )?'
      r'fidl::(Message::|Encoder::|get_alt_type|Decoder::'
      r'|internal::PendingResponse|internal::MessageReader|Binding<'
      r'|InterfaceRequest<[a-zA-Z0-9_:]>::|MessageBuffer::'
      r'|internal::Report[A-Za-z]+Error|internal::ProxyController'
      r'|internal::StubController|InterfacePtr<|EventSender<|BindingSet<'
      r'|Builder::|internal::WeakStubController|FidlTransformWithCallback)');

  @override
  bool match(
      String symbol, CompileUnitContext compileUnit, ProgramContext program) {
    if (compileUnit.isNameHlcppRuntime) return true;

    if (_fidlRegex.hasMatch(symbol)) return true;

    return false;
  }
}

class LlcppRuntimeCategory extends CodeCategory implements SomeFidlCategory {
  const LlcppRuntimeCategory();

  @override
  String get description =>
      'Run-time support libraries for the low-level C++ FIDL bindings';

  static final _fidlRegex =
      RegExp(r'^fidl::(BindingRef::|internal::AsyncBinding::|SimpleBinding::'
          r'|internal::AsyncTransaction::|internal::ResponseContext::'
          r'|(CompleterBase::(?!SendReply<))|Walk|internal::Walk|Allocator::'
          r'|BufferAllocator::|FailoverHeapAllocator::'
          r'|ClientPtr::|ClientBase::|Client<'
          r'|ServerBinding<|BindServer|BindSingleInFlightOnly|FidlMessage::)');

  static final _fidlRegexReturnVoid = RegExp(r'^void fidl::Walk<');

  static final _coreFidlRegex =
      RegExp(r'^fidl_(encode|decode|linearize|linearize_and_encode'
          r'|validate|close_handles|transform)(_msg|_etc)?$');

  @override
  bool match(
      String symbol, CompileUnitContext compileUnit, ProgramContext program) {
    if (compileUnit.isNameLlcppRuntime) return true;

    if (_fidlRegex.hasMatch(symbol)) return true;
    if (_fidlRegexReturnVoid.hasMatch(symbol)) return true;
    if (_coreFidlRegex.hasMatch(symbol)) return true;

    return false;
  }
}

class CFidlContextMixin {
  static final List<RegExp> _cFidl = [
    // fidling/gen/sdk/fidl/fuchsia.device.test/fuchsia/device/test/c/fidl.server.c
    // fidling/gen/zircon/system/fidl/fuchsia-device-manager/fuchsia/device/manager/c/fidl.client.c
    RegExp(r'^fidling/gen/.*/fidl/.*/c/fidl\..*\.c$'),

    // gen/system/fidl/fuchsia-boot/fuchsia-boot.c/server.c
    RegExp(r'^gen/.*/fidl/.*\.c/.*\.c$'),
  ];

  bool get isNameCFidl => _isNameCFidl(this);

  final _isNameCFidl = Lazy<bool, CompileUnitContext>(
      (CompileUnitContext self) => _compileUnitHas(self, _cFidl));
}

class CFidlCategory extends CodeCategory implements SomeFidlCategory {
  const CFidlCategory();

  @override
  String get description => 'C FIDL bindings, excluding coding tables';

  static final RegExp _cFunctions =
      // ignore: prefer_interpolation_to_compose_strings
      RegExp(r'^fuchsia_([a-z0-9]+_)+[A-Z][a-zA-Z0-9]+(?!Table)(' +
          [
            // fuchsia_io_DirectoryReadDirents_reply
            r'_reply$',

            // fuchsia_io_Directory_dispatch
            r'_dispatch$',

            // fuchsia_io_Directory_try_dispatch
            r'_try_dispatch$',

            // fuchsia_io_DirectoryOpen
            r'[A-Z][a-z]+$',
          ].join('|') +
          ')');

  @override
  bool match(
      String symbol, CompileUnitContext compileUnit, ProgramContext program) {
    if (compileUnit.isNameCFidl) return true;

    if (_cFunctions.hasMatch(symbol)) return true;

    return false;
  }
}
