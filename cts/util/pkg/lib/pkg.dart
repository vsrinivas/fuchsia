// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'dart:async';
import 'dart:convert';

import 'package:quiver/core.dart' show Optional;
import 'package:sl4f/sl4f.dart' as sl4f;

/// Make sure there's only one instance of the item in the input.
///
/// Example expected input:
/// Rule {
///     host_match: "fuchsia.com",
///     host_replacement: "devhost",
///     path_prefix_match: "/",
///     path_prefix_replacement: "/",
/// }
///
/// We want to make sure there is only one instance of a given key:value.
bool hasExclusivelyOneItem(
    String input, String expectedKey, String expectedValue) {
  return 1 ==
      RegExp('($expectedKey):\\s+\\"($expectedValue)\\"')
          .allMatches(input)
          .length;
}

/// Find the redirect rule for `fuchsia.com`, if there is one.
///
/// Example expected input:
/// Rule {
///     host_match: "fuchsia.com",
///     host_replacement: "devhost",
///     path_prefix_match: "/",
///     path_prefix_replacement: "/",
/// }
///
/// Returns `Optional.of("devhost")` for the above example input.
/// If there are no rules for `"fuchsia.com"`, will return `Optional.absent()`.
Optional<String> getCurrentRewriteRule(String ruleList) {
  String fuchsiaRule = '';
  var matches = RegExp('Rule (\{.+?\})', dotAll: true).allMatches(ruleList);
  for (final match in matches) {
    if (hasExclusivelyOneItem(match.group(0), 'host_match', 'fuchsia.com')) {
      fuchsiaRule = match.group(0);
      break;
    }
  }
  matches = RegExp('host_replacement:\\s+\\"(.+)\\"').allMatches(fuchsiaRule);
  for (final match in matches) {
    // There should only be one match. If not, just take the first one.
    return Optional.of(match.group(1));
  }
  return Optional.absent();
}

/// Returns the output of `pkgctl repo` as a set.
///
/// Each line in the output is a string in the set.
Future<Set<String>> getCurrentRepos(sl4f.Sl4f sl4fDriver) async {
  var listSrcsResponse = await sl4fDriver.ssh.run('pkgctl repo');
  if (listSrcsResponse.exitCode != 0) {
    return {};
  }
  return Set.from(LineSplitter().convert(listSrcsResponse.stdout.toString()));
}

/// Resets the pkgctl state to its default state.
///
/// Some tests add new package sources that override defaults. We can't
/// trust that they will clean up after themselves, so this function
/// will generically remove all non-original sources and enable the original
/// rewrite rule.
Future<bool> resetPkgctl(sl4f.Sl4f sl4fDriver, Set<String> originalRepos,
    Optional<String> originalRewriteRule) async {
  var currentRepos = await getCurrentRepos(sl4fDriver);

  // Remove all repos that were not originally existing.
  currentRepos.removeAll(originalRepos);
  for (final server in currentRepos) {
    final rmSrcResponse = await sl4fDriver.ssh.run('pkgctl repo rm $server');
    if (rmSrcResponse.exitCode != 0) {
      return false;
    }
  }

  if (originalRewriteRule.isPresent) {
    // TODO: Get this to work:
    // 'pkgctl rule replace json \"\'{\\"version\\": \\"1\\", \\"content\\": [{\\"host_match\\": \\"fuchsia.com\\",\\"host_replacement\\": \\"$originalRewriteRule\\",\\"path_prefix_match\\": \\"/\\",\\"path_prefix_replacement\\": \\"/\\"}] }\'\"'
    // Have been unable to find the correct escape character incantations for
    // the command to run successfully.
    final enableSrcResponse = await sl4fDriver.ssh
        .run('amberctl enable_src -n ${originalRewriteRule.value}');
    if (enableSrcResponse.exitCode != 0) {
      return false;
    }
  }
  return true;
}
