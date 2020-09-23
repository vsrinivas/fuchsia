#!/usr/bin/env python3
#
# Copyright 2020 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Manage submitting a large number of CLs."""

import argparse
import os
import re
import sys
import time

import util
import gerrit_util

from typing import List, Dict, Tuple, Optional, Any

class Change:
  """A Gerrit Changelist."""

  def __init__(self, change_id: str, json: Any):
    self.change_id: str = change_id
    self.status: str = json.get('status', 'UNKNOWN')
    self.subject: str = json.get('subject', '<unknown>')
    self.id: int = int(json.get('_number', '-1'))
    self.json = json

  @classmethod
  def from_json(cls, json: Any) -> 'Change':
    return Change(json['change_id'], json=json)

  def labels(self) -> Dict[str, Any]:
    labels = self.json.get('labels', {})
    assert isinstance(labels, Dict)
    return labels

  def cq_votes(self) -> int:
    cq_labels = self.labels().get('Commit-Queue', {})
    if cq_labels.get('approved') is not None:
      return 2
    if cq_labels.get('recommended') is not None:
      return 1
    return 0

  def has_unresolved_comments(self) -> bool:
    unresolved_comments: int = self.json.get('unresolved_comment_count', 0)
    return unresolved_comments > 0

  def submittable(self) -> bool:
    is_submittable: bool = self.json.get('submittable', False)
    return is_submittable


class GerritServer:
  """A connection to a Gerrit server."""

  DEFAULT_PARAMS = [
      'LABELS', 'ALL_REVISIONS', 'SKIP_DIFFSTAT', 'SUBMITTABLE', 'CHECK'
  ]

  def __init__(self, host: str):
    """Create a connection to the server."""
    self.host = host

  def query(self, query: List[Tuple[str, str]]) -> List[Change]:
    """Query a Gerrit server for changes matching query terms.

    Args:
      query: A list of key:value pairs for search parameters, as documented
        here (e.g. ('is', 'owner') for a parameter 'is:owner'):
        https://gerrit-review.googlesource.com/Documentation/user-search.html#search-operators

    Returns:
      JSON
    """
    return [Change.from_json(x) for x in gerrit_util.QueryChanges(
        self.host, query, o_params=GerritServer.DEFAULT_PARAMS)]

  def fetch_change(self, id: str) -> Optional[Change]:
    """Fetch information about the CL with the given Gerrit ID."""
    result = self.query([('change', id)])
    if len(result) == 1:
      return result[0]
    if len(result) > 1:
      raise Exception('More than one CL had the specified change ID.')
    return None

  def set_cq_state(self, id: str, state: int) -> None:
    """Update the 'Commit-Queue' label to the given vote."""
    gerrit_util.SetReview(
        self.host, id, labels={'Commit-Queue': state}, notify=False)

  def get_change_dependencies(self, id: str) -> List[str]:
    """Get a list of IDs that are dependencies of the given change."""
    # Get related change sets. This includes both children and parent changes.
    json: Any = gerrit_util.GetRelatedChanges(self.host, id)

    # Changes are given in topological order, from newest to oldest.
    #
    # Search through the changes until we see ourselves. Any later changes are dependencies.
    dependencies: List[str] = []
    seen_self = False
    for change in json.get('changes', []):
      # Ignore children of ourselves.
      change_id: str = change['change_id']
      if change_id == id:
        seen_self = True
      if not seen_self:
        continue
      dependencies.append(change_id)
    if not seen_self:
      return [id]
    return dependencies


def should_submit(cl: Change) -> bool:
  return cl.submittable() and not cl.has_unresolved_comments()


def shorten(s: str, max_len: int = 60) -> str:
  """Truncate a long string, appending '...' if a change is made."""
  if len(s) < max_len:
    return s
  return s[:max_len-3] + '...'


def status_string(cl : Change) -> str:
  """Return a CL's status as a human-readable string."""
  if cl.status == 'MERGED':
    return 'merged'
  if cl.status != 'NEW':
    return 'UNKNOWN'
  if cl.has_unresolved_comments():
    return 'UNRESOLVED COMMENT'
  if not cl.submittable():
    return 'MISSING VOTES'
  if cl.cq_votes() == 1:
    return 'testing'
  if cl.cq_votes() == 2:
    return 'submitting'
  return 'ready'


def print_changes(results: List[Change]) -> None:
  """Display a list of results in a table."""
  print()
  print('%20s  %-10s  %-65s' % ('Status', 'CL Number', 'Subject'))
  print('%20s  %-10s  %-65s' % ('――――――', '――――――――――', '―――――――'))
  for result in results:
    print('%20s  %-10s  %-65s' % (status_string(result), result.id, shorten(result.subject, 65)))
  print()


class SubmitError(Exception):
  def __init__(self, message: str):
    self.message: str = message
    super().__init__(message)


def ensure_changes_submittable(changes: List[Change]) -> None:
  """Ensure that the given list of changes are submittable."""
  for cl in changes:
    if not cl.status == 'MERGED' and not should_submit(cl):
      raise SubmitError("CL %d can not be submitted." % cl.id)


def submit_changes(
    clock: util.Clock,
    server: GerritServer,
    changes: List[Change],
    num_retries: int = 0
) -> None:
  # Strip out merged changes.
  changes = [cl for cl in changes if not cl.status == 'MERGED']

  # For any CL that doesn't have a CQ+1, run it now to speed things up.
  # As long as the CL isn't changed in the mean-time, it won't be tested
  # again when we finally get around to +2'ing it.
  #
  # We ignore the first one, because we are just about to +2 it anyway.
  for cl in changes[1:]:
    if cl.cq_votes() == 0:
      print("Setting CQ state of CL %d to dry-run." % cl.id)
      server.set_cq_state(cl.change_id, 1)

  # Submit the changes in order.
  for cl in changes:
    backoff = util.ExponentialBackoff(clock)
    max_attempts = num_retries + 1
    num_attempts = 0
    print()
    print("Submitting CL %d: %s" % (cl.id, cl.subject))

    while True:
      # Fetch the latest information about the CL.
      current_cl = server.fetch_change(cl.change_id)

      # Check it still exists.
      if current_cl is None:
        raise SubmitError("CL %s could not be found." % cl.id)

      # If it is merged, we are done.
      if current_cl.status == 'MERGED':
        break

      # If it is not in CQ, add it to CQ.
      if current_cl.cq_votes() < 2:
        if num_attempts == 0:
          print("  Adding to CQ.")
        elif num_attempts < max_attempts:
          print("  CL failed in CQ. Retrying...")
        else:
          print("  CL failed in CQ. Aborting.")
          return
        num_attempts += 1
        server.set_cq_state(cl.change_id, 2)

      # wait.
      backoff.wait()
      print('  Polling...')

    # Did we fail?
    print("  Submitted!")


def parse_args() -> Any:
  description = r"""
Submit a chain of CLs, specified by giving the CL number of the end of the
chain. The command will poll indefinitely until the chain is submitted or an
error is detected.

The tool can be safely cancelled at any time. When restarted, it will resume
where it left off.

For example, given a chain of three CLs:

  101: Start hacking on 'foo'.
  102: More hacking on 'foo'.
  103: Finish hacking on 'foo'.

The command:

  fx gerrit-submit 103

will:

  1. Add a CQ+1 vote to all the CLs, to start testing them.
  2. Add a CQ+2 vote for the first CL, and wait for it to be submitted.
  3. Add a CQ+2 vote for the second CL, and wait for it to be submitted.
  4. Add a CQ+2 vote for the last CL, and wait for it to be submitted.

Adding a CQ+1 to every CL at the beginning speeds up submission: CQ won't
need to re-test intermediate CLs if they are not modified in the meantime.

If any CL is not ready to submit (for example, it is missing a vote, or has
unresolved comments), the tool will abort early.

By default, the tool will use the "fuchsia-review.googlesource.com" Gerrit
instances. Other instances can be specified using the "--host" parameter:

   fx gerrit-submit --host myteam-review.googlesource.com 12345

"""
  parser = argparse.ArgumentParser(
          description=description,
          formatter_class=argparse.RawDescriptionHelpFormatter)
  parser.add_argument('cl', metavar='CL',
                      help='Gerrit CL to submit. May either be a CL '
                           'number or Gerrit Change-ID.')
  parser.add_argument('--host', dest='host',
                      help='Gerrit host to connect to. '
                           'Defaults to "fuchsia-review.googlesource.com".',
                      default='fuchsia-review.googlesource.com')
  parser.add_argument('--num-retries', metavar='N', type=int, default=0,
                      help='number of times to retry a failed submission. '
                           'Defaults to 0.')
  parser.add_argument('-n', '--dry-run', action='store_true',
                      help='If specified, show the set of CLs that would '
                           'be submitted, but don''t actually submit.')
  parser.add_argument('-t', '--batch', action='store_true',
                      help='If specified, don''t prompt before starting '
                           'submit.')
  return parser.parse_args()


def is_valid_change_id(value: str) -> bool:
  """Determine if "value" is a valid Gerrit CL identifier."""

  # Match strings of the form I609f446e6721dd95624939dd041189052054fb83
  if re.fullmatch('I[a-f0-9]{8,}', value):
    return True

  # Match plain integer change IDs.
  if re.fullmatch('[1-9][0-9]*', value):
    return True

  return False


def should_continue() -> bool:
  """Prompt the user to determine if an action should continue."""
  val = input('Submit CLs? [y/N] ')
  if val.lower()[:1] == 'y':
    return True

  print('Aborting.')
  return False


def main() -> int:
  # Parse and validate arguments.
  args = parse_args()

  # Ensure the Change-ID looks like a Gerrit Change-ID, and not a git commit.
  #
  # TODO: Support Git commit ids and ranges as well.
  if not is_valid_change_id(args.cl):
    print("The argument '%s' does not look like a valid Gerrit Change ID." % args.cl)
    print()
    print('Please provide either the numeric CL number (such as 12345) or\n'
          'the Change-ID found at the bottom of the CL description (such as\n'
          "'I0123456789abcdef0123456789abcdef0123456789abcdef'")
    return 1

  # Check we have valid authentication tokens.
  gerrit_util.EnsureAuthenticated(args.host)

  # Get the specified change, and bail out if we can't find it.
  server = GerritServer(args.host)
  specified_change = server.fetch_change(args.cl)
  if specified_change is None:
      print("Could not find Gerrit commit with id '%s'." % args.cl)
      return 1

  # Fetch the list of dependent CLs, and reverse into the order we need to
  # submit it in.
  change_ids = server.get_change_dependencies(specified_change.change_id)
  change_ids.reverse()

  # Fetch CL details.
  changes = []
  for change_id in change_ids:
    change = server.fetch_change(change_id)
    if change is None:
      print("Could not find CL with id '%s'." % change_id)
      return 1
    changes.append(change)

  # Print the set of changes.
  print_changes(changes)

  # Ensure we can submit the chain.
  ensure_changes_submittable(changes)

  # Submit the changes.
  if not args.dry_run:
    if args.batch or should_continue():
      submit_changes(util.Clock(), server, changes, num_retries=args.num_retries)
      print()
      print('All changes submitted.')

  return 0


if __name__ == '__main__':
  try:
    sys.exit(main())
  except SubmitError as e:
    print('Error: %s' % e.message)
  except gerrit_util.GerritError as e:
    print('Error: %s' % e.message)
  sys.exit(1)

