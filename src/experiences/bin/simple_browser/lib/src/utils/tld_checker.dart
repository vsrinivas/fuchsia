// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'package:fuchsia_logger/logger.dart';
import 'package:meta/meta.dart';
import 'tlds_provider.dart';
import 'valid_tlds.dart';

class TldChecker {
  List<String> _validTlds;

  /// A flag that indicates if the valid TLD list is loaded or not.
  ///
  /// Its default value on the initilization is 'false'. and is set to 'true'
  /// once the [prefetchTlds()] is called, and never changes unless the browser
  /// is relaunched and this [TldChecker] is newly initiated.
  bool _isIanaTldsLoaded;

  static final TldChecker _tldCheckerInstance = TldChecker._create();
  factory TldChecker() {
    return _tldCheckerInstance;
  }

  TldChecker._create() {
    _validTlds = kValidTlds;
    _isIanaTldsLoaded = false;
    log.info('A singleton TldChecker instance has been created.');
  }

  /// Fetches a valid TLD list from the IANA if it has not loaded yet.
  ///
  /// If a List<String> type parameter is given, it does not fetch the TLD list
  /// from the web and instead, just uses the parameter list as the valid TLD
  /// list. Therefore, this parameter should be given only for testing purposes.
  void prefetchTlds({List<String> testTlds}) async {
    if (testTlds != null) {
      _validTlds = testTlds;
    } else {
      if (!_isIanaTldsLoaded) {
        _validTlds = await TldsProvider().fetchTldsList() ?? kValidTlds;
        _isIanaTldsLoaded = true;
      } else {
        log.warning(
            'TLD List is already loaded. You do not need to fetch it again.');
      }
    }
  }

  bool isValid(String tld) => _validTlds.contains(tld.toUpperCase());

  @visibleForTesting
  List<String> get validTlds => _validTlds;
}
