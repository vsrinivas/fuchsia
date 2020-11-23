// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:test/test.dart';
import 'package:codesize/queries/index.dart';
import 'package:codesize/queries/categories/fidl_rust.dart';

void main() {
  group('match symbols', () {
    void pos(String name) {
      const rust = RustFidlCategory();
      final compileUnit = CompileUnitContext('foobar.rs');
      final program = ProgramContext('foobar', null);

      expect(rust.match(name, compileUnit, program), equals(true),
          reason: '`$name` should match one of the symbol name regexes '
              'for Rust FIDL code');
    }

    // These are derived from the comments in 'fidl_rust.dart'.
    test('Positive cases', () {
      pos(r'<fidl::endpoints::ServerEnd<fidl_fuchsia_io::NodeMarker>>::into_stream');
      pos(r'<fidl::endpoints::ServerEnd<fidl_fuchsia_io::NodeMarker>>::into_stream_and_control_handle');

      pos(r'fidl::endpoints::create_proxy::<fidl_fuchsia_io::DirectoryMarker>');
      pos(r'fidl::endpoints::create_endpoints::<fidl_fuchsia_io::DirectoryMarker>');
      pos(r'core::ptr::drop_in_place::<fidl_fuchsia_io::FileRequest>');
      pos(r'core::ptr::drop_in_place::<fidl::error::Error>');

      pos(r'<&fidl_fuchsia_kernel::CpuStats as core::fmt::Debug>::fmt<&fidl_fuchsia_io2::Operations as core::hash::Hash>::hash::<std::collections::hash::map::DefaultHasher>');
      pos(r'std::future::poll_with_tls_context::<futures_util::stream::try_stream::try_next::TryNext<fidl_fuchsia_sys2::SystemControllerRequestStream>>');
      pos(r'<&mut fidl_fuchsia_wlan_policy::ClientProviderRequestStream as futures_core::stream::FusedStream>::is_terminated');
      pos(r'<futures_util::stream::try_stream::try_for_each::TryForEach<fidl_fuchsia_devicesettings::DeviceSettingsManagerRequestStream, futures_util::future::ready::Ready<core::result::Result<(), fidl::error::Error>>, device_settings_manager::spawn_device_settings_server::{closure#0}> as core::future::future::Future>::poll');

      pos(r'switch.table.<fidl_fuchsia_io::NodeInfo as fidl::encoding::Decodable>::decode');
    });

    void neg(String name) {
      const rust = RustFidlCategory();
      final compileUnit = CompileUnitContext('foobar.rs');
      final program = ProgramContext('foobar', null);

      expect(rust.match(name, compileUnit, program), equals(false),
          reason: '`$name` should not match any of the symbol name regexes '
              'for Rust FIDL code');
    }

    test('Negative cases', () {
      neg(r'');
      neg(r' ');
      neg(r'core::ptr::drop_in_place::<something_that_is::NotAFidlRequest>');
    });
  });
}
