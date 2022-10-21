// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:fidl/fidl.dart';
import 'package:fidl_fuchsia_intl/fidl_async.dart';
import 'package:flutter/material.dart';
import 'package:fuchsia_logger/logger.dart';
import 'package:intl/intl.dart';

// The locale to use if service-based locale resolution fails.
const _defaultLocale =
    Locale.fromSubtags(languageCode: 'en', countryCode: 'US');

/// Extracts the user's preferred locale from the profile.
Locale _fromProfile(Profile profile) {
  final String localeName =
      profile.locales?.first.id ?? _defaultLocale.toString();
  // This is not quite correct, but will likely be enough for
  // our purposes for the time being.
  final String canonicalized =
      Intl.canonicalizedLocale(localeName).split('-u-').first;
  Iterator<String?> split = canonicalized.split('_').iterator;
  String languageCode = 'en';
  if (split.moveNext()) {
    languageCode = split.current ?? 'en';
  }
  String? countryCode = null;
  if (split.moveNext()) {
    countryCode = split.current;
  }
  // Note, Locale('en_US') is not a correctly initialized Dart locale.
  // You must separate tags manually.
  return Locale.fromSubtags(
      languageCode: languageCode, countryCode: countryCode);
}

/// Encapsulates the logic to obtain the locales from the service
/// fuchsia.intl.PropertyProvider.
///
/// For the time being, the first locale only is returned.
class LocaleSource {
  final PropertyProvider _stub;

  /// Constructs a new LocaleSource, with an established client connection to
  /// `fuchsia.intl.PropertyProvider` provided by the stub.
  const LocaleSource(this._stub);

  /// Returns the stream of all locale changes, including the initial locale
  /// value.
  Stream<Locale> stream() async* {
    yield await _initial();
    await for (var locale in _changes()) {
      yield locale;
    }
  }

  // Returns the initial locale from the service.
  Future<Locale> _initial() async {
    try {
      return _fromProfile(await _stub.getProfile());
    } on FidlError catch (e, s) {
      log.warning(
          'Could not get locale from fuchsia.intl.ProfileProvider for the shell. '
          'This is nonfatal, but the shell will not support any locale '
          'except for the system default: $e: $s');
      return _defaultLocale;
    }
  }

  // Returns the stream of locale changes, after the call to [_initial()].
  Stream<Locale> _changes() {
    return _stub.onChange!
        .asyncMap((x) => _stub.getProfile())
        .map(_fromProfile);
  }
}
