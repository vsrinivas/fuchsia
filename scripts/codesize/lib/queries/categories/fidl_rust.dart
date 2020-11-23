// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// @dart = 2.8

import 'dart:core';

import '../code_category.dart';
import '../index.dart';

bool _compileUnitHas(CompileUnitContext compileUnit, List<RegExp> regexp) {
  if (!compileUnit.isRust) return false;
  if (matchRegexEnsureAtMostOne(compileUnit.name, regexp)) return true;
  return false;
}

class RustFidlContextMixin {
  static final List<RegExp> _regex = [
    // fidling/gen/sdk/fidl/fuchsia.media/fidl_fuchsia_media.rs
    // fidling/gen/sdk/fidl/fuchsia.bluetooth.avdtp/fidl_fuchsia_bluetooth_avdtp.rs
    // fidling/gen/zircon/system/fidl/fuchsia-io/fidl_fuchsia_io.rs
    RegExp(r'^fidling/gen/.*/fidl/?.*/fidl_.*\.rs$'),

    // ../../src/lib/fidl/rust/fidl/src/lib.rs
    RegExp(r'^\.\./\.\./src/lib/fidl/rust'),

    // [crate: fidl_fuchsia_io]
    // [crate: fidl]
    RegExp(r'^\[crate: fidl(_[a-z][a-z0-9_]+)?\]$'),
  ];

  bool get isNameRustFidl => _isNameRustFidl(this);

  final _isNameRustFidl = Lazy<bool, CompileUnitContext>(
      (CompileUnitContext self) => _compileUnitHas(self, _regex));
}

class RustFidlCategory extends CodeCategory implements SomeFidlCategory {
  const RustFidlCategory();

  @override
  String get description =>
      'Rust FIDL bindings (both the runtime and generated code)';

  static final List<RegExp> _regexes = [
    // <fidl::endpoints::ServerEnd<fidl_fuchsia_io::NodeMarker>>::into_stream
    // <fidl::endpoints::ServerEnd<fidl_fuchsia_io::NodeMarker>>::into_stream_and_control_handle
    // <fidl::endpoints::ClientEnd<fidl_fuchsia_io::DirectoryMarker>>::into_proxy
    RegExp(
        r'^<fidl::endpoints::(Client|Server)End<fidl_[a-z0-9_]+::.*Marker>>::'),

    // fidl::endpoints::create_proxy::<fidl_fuchsia_io::DirectoryMarker>
    RegExp(r'^fidl::endpoints::create_proxy::<fidl_[a-z0-9_]+::.*Marker>'),

    // fidl::endpoints::create_endpoints::<fidl_fuchsia_io::DirectoryMarker>
    RegExp(r'^fidl::endpoints::create_endpoints::<fidl_[a-z0-9_]+::.*Marker>'),

    // core::ptr::drop_in_place::<fidl_fuchsia_io::FileRequest>
    // core::ptr::drop_in_place::<
    //     core::result::Result<
    //         fidl_fuchsia_io::DirectoryRequest,
    //         fidl::error::Error
    //     >
    // >
    // core::ptr::drop_in_place::<fidl::error::Error>
    RegExp(
        r'^core::ptr::drop_in_place::(<fidl::error::Error>|.*<fidl_[a-z0-9_]+::[a-zA-Z0-9]+Request[>,])'),

    // <&fidl_fuchsia_kernel::CpuStats as core::fmt::Debug>::fmt
    // <
    //     &fidl_fuchsia_io2::Operations as core::hash::Hash
    // >::hash::<std::collections::hash::map::DefaultHasher>
    RegExp(r'^<&fidl_[a-z0-9_]+::.* as core::(fmt|hash).*>::(fmt|hash)'),

    // <
    //     &mut fidl_fuchsia_wlan_policy::ClientProviderRequestStream as futures_core::stream::FusedStream
    // >::is_terminated
    RegExp(r'^<&mut fidl_[a-z0-9_]+::[a-zA-Z0-9_]+RequestStream '
        r'as futures_core::stream::FusedStream>::is_terminated$'),

    // <
    //     futures_util::stream::try_stream::try_for_each::TryForEach<
    //         fidl_fuchsia_devicesettings::DeviceSettingsManagerRequestStream,
    //         futures_util::future::ready::Ready<
    //             core::result::Result<
    //                 (),
    //                 fidl::error::Error
    //             >
    //         >,
    //         device_settings_manager::spawn_device_settings_server::{closure#0}
    //     > as core::future::future::Future
    // >::poll
    RegExp(r'^<futures_util::stream::try_stream::try_for_each::TryForEach<'
        r'fidl_[a-z0-9_]+::[a-zA-Z0-9_]+RequestStream, futures_util::future::ready.*>::poll$'),

    // <
    //     futures_util::stream::try_stream::try_next::TryNext<
    //         fidl_fuchsia_inspect::TreeNameIteratorRequestStream
    //     > as core::future::future::Future
    // >::poll
    RegExp(r'^<futures_util::stream::try_stream::try_next::TryNext<'
        r'fidl_[a-z0-9_]+::[a-zA-Z0-9_]+RequestStream> as core::future::future::Future'
        r'>::poll$'),

    // <
    //     core::result::Result<
    //         (fidl_fuchsia_io::NodeRequestStream, fidl_fuchsia_io::NodeControlHandle),
    //         fidl::error::Error
    //     > as anyhow::Context<
    //         (fidl_fuchsia_io::NodeRequestStream, fidl_fuchsia_io::NodeControlHandle),
    //         fidl::error::Error
    //     >
    // >::context::<&str>
    RegExp(r'^<core::result::Result<'
        r'\(fidl_[a-z0-9_]+::[a-zA-Z0-9_]+RequestStream, fidl_[a-z0-9_]+::[a-zA-Z0-9_]+ControlHandle\), '
        r'fidl::error::Error>.*::context::'),

    // std::future::poll_with_tls_context::<
    //     futures_util::stream::try_stream::try_next::TryNext<
    //         fidl_fuchsia_sys2::SystemControllerRequestStream
    //     >
    // >
    // std::future::poll_with_tls_context::<
    //     futures_util::stream::stream::next::Next<
    //         fidl_fuchsia_io::FileRequestStream
    //     >
    // >
    RegExp(r'^std::future::poll_with_tls_context::<'
        r'futures_util::stream::(try_)?stream::(try_)?next::(Try)?Next<'
        r'fidl_[a-z0-9_]+::[a-zA-Z0-9_]+RequestStream>>'),

    // switch.table.<fidl_fuchsia_io::NodeInfo as fidl::encoding::Decodable>::decode
    RegExp(r'^switch.table.<fidl_[a-z0-9_]+::.* as fidl::'),
  ];

  @override
  bool match(
      String symbol, CompileUnitContext compileUnit, ProgramContext program) {
    // Note that we are delibrately not checking if `program.lang` is Rust,
    // because some Rust apps might be mis-classified as C++ apps, if they
    // link in a large amount of C++ libraries.

    // Make sure the current compile unit is Rust.
    if (compileUnit.isNameRustFidl) return true;

    if (compileUnit.isCFamilyFileExtension) return false;
    if (matchRegexEnsureAtMostOne(symbol, _regexes)) return true;

    return false;
  }
}
