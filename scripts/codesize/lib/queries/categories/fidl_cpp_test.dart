// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:test/test.dart';

import '../index.dart';
import 'fidl_cpp.dart';

void main() {
  group('isNameCppCodingTables', () {
    void pos(String name) =>
        expect(CompileUnitContext(name).isNameCppCodingTables, equals(true),
            reason: '`$name` should match one of the compile unit name regexes '
                'for coding tables');
    test('Positive cases', () {
      pos('fidling/gen/sdk/fidl/fuchsia.feedback/fuchsia.feedback.fidl.tables.c');
      pos('fidling/gen/zircon/system/fidl/fuchsia-mem/fuchsia-mem.fidl.tables.c');
      pos('gen/build/fuchsia/fidl/fuchsia/ui/gfx/cpp/tables.c');
      pos('gen/third_party/fuchsia-sdk/sdk/fidl/fuchsia.io/fidl/fuchsia.io.fidl-tables.c');
      pos('gen/system/fidl/fuchsia-io/fuchsia-io.tables/tables.c');
      pos('gen/fuchsia/fidl/some.custom.lib.fidl-tables.c');
      pos('gen/foo/bar/fuchsia/fidl/some.custom.lib/fidl/some.custom.lib.fidl-tables.c');
      pos('bla/bla/bla/fuchsia/sdk/fidl/fuchsia_net/fuchsia_net_tables.c');
    });

    Function neg(String name) => () => expect(
        CompileUnitContext(name).isNameCppCodingTables, equals(false),
        reason: '`$name` should not match any of the compile unit name regexes '
            'for coding tables');
    test('Empty', neg(''));
    test('Untraceable', neg('[LOAD #4 [RW]]'));
    test('HLCPP generated source file',
        neg('gen/third_party/fuchsia-sdk/sdk/fidl/fuchsia.io/fidl/fuchsia/io/cpp/fidl.cc'));
    test('LLCPP generated source file',
        neg('fidling/gen/zircon/system/fidl/fuchsia-kernel/fuchsia/kernel/llcpp/fidl.cc'));
  });

  group('isNameHlcppDomainObject', () {
    void pos(String name) =>
        expect(CompileUnitContext(name).isNameHlcppDomainObject, equals(true),
            reason: '`$name` should match one of the compile unit name regexes '
                'for HLCPP domain objects');
    test('Positive cases', () {
      pos('fidling/gen/sdk/fidl/fuchsia.feedback/fuchsia/feedback/cpp/fidl.cc');
      pos('fidling/gen/src/connectivity/bluetooth/fidl/fuchsia/bluetooth/host/cpp/fidl.cc');
      pos('gen/third_party/fuchsia-sdk/sdk/fidl/fuchsia.modular/fidl/fuchsia/modular/cpp/fidl.cc');
      pos('gen/build/fuchsia/fidl/fuchsia/accessibility/semantics/cpp/fidl.cc');
      pos('gen/foo/internal/fuchsia/fidl/some.lib/fidl/some/lib/cpp/fidl.cc');
      pos('third_party/some_toolchain_dir/fuchsia/sdk/fidl/fuchsia_io/fuchsia_io_cc_codegen.cc/fuchsia/io/cpp/fidl.cc');
    });

    Function neg(String name) => () => expect(
        CompileUnitContext(name).isNameHlcppDomainObject, equals(false),
        reason: '`$name` should not match any of the compile unit name regexes '
            'for HLCPP domain objects');
    test('Empty', neg(''));
    test('Untraceable', neg('[LOAD #4 [RW]]'));
    test('Coding table generated source file',
        neg('fidling/gen/sdk/fidl/fuchsia.feedback/fuchsia.feedback.fidl.tables.c'));
    test('LLCPP generated source file',
        neg('fidling/gen/zircon/system/fidl/fuchsia-kernel/fuchsia/kernel/llcpp/fidl.cc'));
  });

  group('HlcppDomainObjectCategory.match', () {
    const category = HlcppDomainObjectCategory();
    final compileUnit = CompileUnitContext('foobar.cc');
    final program = ProgramContext('foobar', null);

    void pos(String name) =>
        expect(category.match(name, compileUnit, program), equals(true),
            reason: '`$name` should match one of the symbol name regexes '
                'for HLCPP domain objects');
    test('Positive cases', () {
      pos('fuchsia::io::Directory_Proxy::Directory_Proxy(fidl::internal::ProxyController*)');
      pos('fuchsia::io::Directory_Stub::Dispatch_(fidl::Message, fidl::internal::PendingResponse)');
      pos('fuchsia::io::Directory_Proxy::Dispatch_(fidl::Message)');
      pos('fuchsia::io::NodeInfo::Encode(fidl::Encoder*, unsigned long)');
      pos('fuchsia::io::NodeInfo::Decode(fidl::Decoder*, fuchsia::io::NodeInfo*, unsigned long)');
      pos('fuchsia::io::NodeInfo::EnsureStorageInitialized(unsigned long)');
      pos('fuchsia::io::Directory_RequestEncoder::Open(fidl::Encoder*, foo)');
      pos('fuchsia::io::Directory_RequestDecoder::GetType(unsigned long, bool*)');
      pos('fuchsia::io::(anonymous namespace)::Directory_Link_ResponseHandler::OnMessage(fidl::Message)');
      pos(r'int fidl::internal::SingleUseMessageHandler::InvokeImpl<fuchsia::io::(anonymous namespace)::Directory_NodeGetFlags_ResponseHandler(fit::function_impl<16ul, false, void (int, unsigned int)>)::$_28>(fidl::internal::SingleUseMessageHandler*, fidl::Message&&)');
    });

    Function neg(String name) =>
        () => expect(category.match(name, compileUnit, program), equals(false),
            reason: '`$name` should not match any of the symbol name regexes '
                'for HLCPP domain objects');
    test('Empty', neg(''));
    test('LLCPP',
        neg('llcpp::fuchsia::io::Directory::Call(fidl::DecodedMessage)'));
  });

  group('isNameHlcppRuntime', () {
    void pos(String name) =>
        expect(CompileUnitContext(name).isNameHlcppRuntime, equals(true),
            reason: '`$name` should match one of the compile unit name regexes '
                'for HLCPP runtime');
    test('Positive cases', () {
      pos('../../fuchsia/sdk/linux/pkg/fidl_cpp_sync/internal/synchronous_proxy.cc');
      pos('../../sdk/lib/fidl/cpp/internal/message_reader.cc');
      pos('../../third_party/fuchsia-sdk/sdk/pkg/fidl_cpp/internal/message_reader.cc');
      pos('third_party/unsupported_toolchains/fuchsia/sdk/pkg/fidl_cpp/internal/message_reader.cc');
      pos('../../fuchsia/sdk/linux/pkg/fidl_cpp/internal/message_reader.cc');
    });
  });

  group('isNameLlcppRuntime', () {
    void pos(String name) =>
        expect(CompileUnitContext(name).isNameLlcppRuntime, equals(true),
            reason: '`$name` should match one of the compile unit name regexes '
                'for LLCPP runtime');
    void neg(String name) => expect(
        CompileUnitContext(name).isNameLlcppRuntime, equals(false),
        reason: '`$name` should not match any of the compile unit name regexes '
            'for LLCPP runtime');
    test('Positive cases', () {
      pos('../../fuchsia/sdk/linux/pkg/fidl_base/decoding.cc');
      pos('../../third_party/fuchsia-sdk/sdk/pkg/fidl_base/decoding.cc');
      pos('../../zircon/system/ulib/fidl/linearizing.cc');
      pos('../../zircon/system/ulib/fidl-async/llcpp_bind.cc');
    });

    test('Negative cases', () {
      neg('../../zircon/system/ulib/fidl/message.cc');
      neg('../../zircon/system/ulib/fidl/message_buffer.cc');
      neg('../../zircon/system/ulib/fidl/builder.cc');
    });
  });

  group('HlcppRuntime.match', () {
    const category = HlcppRuntimeCategory();
    final compileUnit = CompileUnitContext('foobar.cc');
    final program = ProgramContext('foobar', null);

    void pos(String name) =>
        expect(category.match(name, compileUnit, program), equals(true),
            reason: '`$name` should match one of the symbol name regexes '
                'for HLCPP runtime');
    test('Positive cases', () {
      pos(r'fidl::FidlTransformWithCallback(unsigned int, fidl_type const*, unsigned char const*, unsigned int, char const**, std::__2::function<int (unsigned char const*, unsigned int)> const&)');
    });

    Function neg(String name) =>
        () => expect(category.match(name, compileUnit, program), equals(false),
            reason: '`$name` should not match any of the symbol name regexes '
                'for HLCPP runtime');
    test('LLCPP Domain Object',
        neg(r'fidl::DecodeResult<llcpp::fuchsia::hardware::usb::peripheral::Device::SetConfigurationRequest> fidl::Decode<llcpp::fuchsia::hardware::usb::peripheral::Device::SetConfigurationRequest>(fidl::EncodedMessage<llcpp::fuchsia::hardware::usb::peripheral::Device::SetConfigurationRequest>)'));
  });
}
