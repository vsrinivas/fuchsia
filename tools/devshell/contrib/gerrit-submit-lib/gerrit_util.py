# Copyright 2020 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""
Utilities for requesting information for a Gerrit server via HTTPS.

https://gerrit-review.googlesource.com/Documentation/rest-api.html
"""

import base64
import http.client
import http.cookiejar as cookielib
import io
import json
import logging
import os
import re
import subprocess
import sys
import urllib
import urllib.error
import urllib.parse
import urllib.request

from typing import Tuple, Dict, Optional, List, Any, FrozenSet, Set, cast

import util


LOGGER = logging.getLogger()

# Maximum number of times to retry a failing HTTP request.
_MAX_HTTP_RETRIES = 5

# Controls the transport protocol used to communicate with Gerrit.
# This is parameterized primarily to enable GerritTestCase.
GERRIT_PROTOCOL = 'https'


def read_file(path: str) -> str:
  """Read the contents of the given file as a string."""
  with open(path, 'rb') as f:
    return f.read().decode('utf-8', errors='surrogateescape')


class GerritError(Exception):
  """Exception class for errors commuicating with the gerrit-on-borg service."""
  def __init__(self, http_status: int, message: str):
    self.http_status = http_status
    self.message = '(%d) %s' % (self.http_status, message)
    super().__init__(self.message)


def _QueryString(params, first_param=None):
  """Encodes query parameters in the key:val[+key:val...] format specified here:

  https://gerrit-review.googlesource.com/Documentation/rest-api-changes.html#list-changes
  """
  q = [urllib.parse.quote(first_param)] if first_param else []
  q.extend(['%s:%s' % (key, val) for key, val in params])
  return '+'.join(q)


class Authenticator:
  """Authenticator implementation that uses ".gitcookies" for token."""

  def __init__(self) -> None:
    # Credentials will be loaded lazily on first use. This ensures Authenticator
    # get() can always construct an authenticator, even if something is broken.
    # This allows 'creds-check' to proceed to actually checking creds later,
    # rigorously (instead of blowing up with a cryptic error if they are wrong).
    self._gitcookies: Optional[Dict[str, Tuple[str, str]]] = None

  @property
  def gitcookies(self) -> Dict[str, Tuple[str, str]]:
    if self._gitcookies is None:
      self._gitcookies = self._get_gitcookies()
    return self._gitcookies

  @classmethod
  def get_new_password_url(cls, host: str) -> str:
    """Generate a URL to instructions for setting up a ".gitcookies" entry."""
    assert not host.startswith('http')
    # Assume *.googlesource.com pattern.
    parts = host.split('.')
    if not parts[0].endswith('-review'):
      parts[0] += '-review'
    return 'https://%s/new-password' % ('.'.join(parts))

  @classmethod
  def get_new_password_message(cls, host: str) -> str:
    if host is None:
      return ('Git host for Gerrit upload is unknown. Check your remote '
              'and the branch your branch is tracking. This tool assumes '
              'that you are using a git server at *.googlesource.com.')
    url = cls.get_new_password_url(host)
    return 'You can (re)generate your credentials by visiting %s' % url

  @classmethod
  def get_gitcookies_path(cls) -> str:
    # Read from the environment.
    env_path = os.getenv('GIT_COOKIES_PATH')
    if env_path is not None:
      return env_path

    # Attempt to read from git's config.
    try:
      path = subprocess.check_output(
          ['git', 'config', '--path', 'http.cookiefile'])
      return path.decode('utf-8', 'ignore').strip()
    except subprocess.CalledProcessError:
      # Guess a default.
      return os.path.expanduser(os.path.join('~', '.gitcookies'))

  @classmethod
  def _get_gitcookies(cls) -> Dict[str, Tuple[str, str]]:
    # Read the cookies file.
    path = cls.get_gitcookies_path()
    if not os.path.exists(path):
      return {}
    try:
      f = read_file(path)
    except IOError:
      return {}

    # Parse each line.
    gitcookies: Dict[str, Tuple[str, str]] = {}
    for line in f.splitlines():
      try:
        fields = line.strip().split('\t')
        if line.strip().startswith('#') or len(fields) != 7:
          continue
        domain, xpath, key, value = fields[0], fields[2], fields[5], fields[6]
        if xpath == '/' and key == 'o':
          if value.startswith('git-'):
            login, secret_token = value.split('=', 1)
            gitcookies[domain] = (login, secret_token)
          else:
            gitcookies[domain] = ('', value)
      except (IndexError, ValueError, TypeError) as exc:
        LOGGER.warning(exc)
    return gitcookies

  def _get_auth_for_host(self, host: str) -> Optional[Tuple[str, str]]:
    for domain, creds in self.gitcookies.items():
      if cookielib.domain_match(host, domain): # type: ignore
        return creds
    return None

  def get_auth_header(self, host: str) -> Optional[str]:
    a = self._get_auth_for_host(host)
    if a:
      if a[0]:
        secret = base64.b64encode(('%s:%s' % (a[0], a[1])).encode('utf-8'))
        return 'Basic %s' % secret.decode('utf-8')
      else:
        return 'Bearer %s' % a[1]
    return None

  def get_auth_email(self, host: str) -> Optional[str]:
    """Best effort parsing of email to be used for auth for the given host."""
    a = self._get_auth_for_host(host)
    if not a:
      return None
    login = a[0]
    # login typically looks like 'git-xxx.example.com'
    if not login.startswith('git-') or '.' not in login:
      return None
    username, domain = login[len('git-'):].split('.', 1)
    return '%s@%s' % (username, domain)


def EnsureAuthenticated(host: str) -> None:
  """Attempt to determine if we are authenticated with Gerrit server."""
  # See if we have an authentication header available for the given host.
  auth = Authenticator()
  if auth.get_auth_header(host):
    return

  # If not, print instructions and quit.
  print(
      'Could not find credentials for host "%(host)s".\n'
      '\n'
      'Credentials are read from %(filename)s.\n'
      '\n'
      '%(instructions)s' % {
        'host': host,
        'filename': auth.get_gitcookies_path(),
        'instructions': auth.get_new_password_message(host),
      })
  sys.exit(1)


class GerritHttpRequest:
  """A HTTP request to a URL that can be issued multiple times."""

  def __init__(self, url: str, data: bytes = None,
               headers: Optional[Dict[str, str]] = None, method: str = 'GET'):
    self.url: str = url
    self.data: Optional[bytes] = data
    self.headers: Dict[str, str] = headers or {}
    self.method: str = method

  @property
  def host(self) -> str:
    return urllib.parse.urlparse(self.url).netloc

  def execute(self) -> Tuple[http.client.HTTPResponse, bytes]:
    request = urllib.request.Request(self.url, data=self.data,
        headers=self.headers, method=self.method)
    try:
      response = urllib.request.urlopen(request)
      return (response, response.read())
    except urllib.error.HTTPError as e:
      return (cast(http.client.HTTPResponse, e), e.read())


def _SendGerritHttpRequest(
    host: str,
    path: str,
    reqtype: str = 'GET',
    headers: Optional[Dict[str, str]] = None,
    body: Any = None,
    accept_statuses: FrozenSet[int] = frozenset([200]),
) -> io.StringIO:
  """Send a request to the given Gerrit host.

  Args:
    host: Gerrit host to connect to.
    path: Path to send request to.
    reqtype: HTTP request type (or, "HTTP verb").
    body: JSON-encodable object to send.
    accept_statuses: Treat any of these statuses as success. Default: [200]
                     Common additions include 204, 400, and 404.

  Returns: A string buffer containing the connection's reply.
  """
  headers = headers or {}
  bare_host = host.partition(':')[0]

  # Set authentication header if available.
  a = Authenticator().get_auth_header(bare_host)
  if a:
    headers.setdefault('Authorization', a)

  # If we have an authentication header, use an authenticated URL path.
  #
  # From Gerrit docs: "Users (and programs) can authenticate with HTTP
  # passwords by prefixing the endpoint URL with /a/. For example to
  # authenticate to /projects/, request the URL /a/projects/. Gerrit will use
  # HTTP basic authentication with the HTTP password from the user’s account
  # settings page".
  url = path
  if not url.startswith('/'):
    url = '/' + url
  if 'Authorization' in headers and not url.startswith('/a/'):
    url = '/a%s' % url

  body_bytes: Optional[bytes] = None
  if body:
    body_bytes = json.dumps(body, sort_keys=True).encode('utf-8')
    headers.setdefault('Content-Type', 'application/json')

  # Create a request
  request = GerritHttpRequest(
      urllib.parse.urljoin('%s://%s' % (GERRIT_PROTOCOL, host), url),
      data=body_bytes,
      headers=headers,
      method=reqtype)

  # Send the request, retrying if there are transient errors.
  backoff = util.ExponentialBackoff(
      util.Clock(), min_poll_seconds=10., max_poll_seconds=600.)
  attempts = 0
  while True:
    attempts += 1

    # Attempt to perform the fetch.
    response, contents_bytes = request.execute()
    contents = contents_bytes.decode('utf-8', 'replace')

    # If we have a valid status, return the contents.
    if response.status in accept_statuses:
      return io.StringIO(contents)

    # If the error looks transient, retry.
    #
    # We treat the following errors as transient:
    #   * Internal server errors (>= 500)
    #   * Errors caused by conflicts (409 "conflict").
    #   * Quota issues (429 "too many requests")
    if ((response.status >= 500 or response.status in [409, 429]) and
        attempts < _MAX_HTTP_RETRIES):
      LOGGER.warn('A transient error occurred while querying %s (%s): %s',
                  request.url, request.method, response.msg)
      backoff.wait()
      continue

    LOGGER.debug('got response %d for %s %s', response.status, request.method,
                 request.url)

    # If we got a 400 error ("bad request"), that may indicate bad authentication.
    #
    # Add some more context.
    if response.status == 400:
      raise GerritError(
          response.status, 'HTTP Error: %s: %s.\n\n'
          'This may indicate a bad request (likely caused by a bug) '
          'or that authentication failed (Check your ".gitcookies" file.)' %
          (response.msg, contents.strip()))

    # Otherwise, throw a generic error.
    raise GerritError(response.status,
                      'HTTP Error: %s: %s' % (response.msg, contents.strip()))


def _SendGerritJsonRequest(
    host: str,
    path: str,
    reqtype: str = 'GET',
    headers: Optional[Dict[str, str]] = None,
    body: Any = None,
    accept_statuses: FrozenSet[int] = frozenset([200]),
) -> Optional[Any]:
  """Send a request to Gerrit, expecting a JSON response."""
  result = _SendGerritHttpRequest(
      host, path, reqtype, headers, body, accept_statuses)

  # The first line of the response should always be: )]}'
  s = result.readline()
  if s and s.rstrip() != ")]}'":
    raise GerritError(200, 'Unexpected json output: %s' % s)

  # Read the rest of the response.
  s = result.read()
  if not s:
    return None
  return json.loads(s)


def QueryChanges(
    host: str,
    params: List[Tuple[str, str]],
    first_param: Optional[Any] = None,
    limit: Optional[int] = None,
    o_params: Optional[List[Any]] = None,
    start: Optional[int] = None
) -> List[Any]:
  """
  Queries a gerrit-on-borg server for changes matching query terms.

  Args:
    params: A list of key:value pairs for search parameters, as documented
        here (e.g. ('is', 'owner') for a parameter 'is:owner'):
        https://gerrit-review.googlesource.com/Documentation/user-search.html#search-operators
    first_param: A change identifier
    limit: Maximum number of results to return.
    start: how many changes to skip (starting with the most recent)
    o_params: A list of additional output specifiers, as documented here:
        https://gerrit-review.googlesource.com/Documentation/rest-api-changes.html#list-changes

  Returns:
    A list of json-decoded query results.
  """
  # Note that no attempt is made to escape special characters; YMMV.
  if not params and not first_param:
    raise RuntimeError('QueryChanges requires search parameters')
  path = 'changes/?q=%s' % _QueryString(params, first_param)
  if start:
    path = '%s&start=%s' % (path, start)
  if limit:
    path = '%s&n=%d' % (path, limit)
  if o_params:
    path = '%s&%s' % (path, '&'.join(['o=%s' % p for p in o_params]))
  try:
    response = _SendGerritJsonRequest(host, path)
  except GerritError as e:
    if e.http_status == 404:
      # Not found.
      return []
    raise
  if response is None:
    raise GerritError(200, 'No response from Gerrit.')
  return response


def GetGerritFetchUrl(host):
  """Given a Gerrit host name returns URL of a Gerrit instance to fetch from."""
  return '%s://%s/' % (GERRIT_PROTOCOL, host)


def GetCodeReviewTbrScore(host, project):
  """Given a Gerrit host name and project, return the Code-Review score for TBR."""
  conn = CreateHttpConn(
      host, '/projects/%s' % urllib.parse.quote(project, ''))
  project = ReadHttpJsonResponse(conn)
  if ('labels' not in project
      or 'Code-Review' not in project['labels']
      or 'values' not in project['labels']['Code-Review']):
    return 1
  return max([int(x) for x in project['labels']['Code-Review']['values']])


def GetChangePageUrl(host, change_number):
  """Given a Gerrit host name and change number, returns change page URL."""
  return '%s://%s/#/c/%d/' % (GERRIT_PROTOCOL, host, change_number)


def GetChangeUrl(host, change):
  """Given a Gerrit host name and change ID, returns a URL for the change."""
  return '%s://%s/a/changes/%s' % (GERRIT_PROTOCOL, host, change)


def GetChange(host, change):
  """Queries a Gerrit server for information about a single change."""
  path = 'changes/%s' % change
  return _SendGerritJsonRequest(host, path)


def GetChangeDetail(host, change, o_params=None):
  """Queries a Gerrit server for extended information about a single change."""
  path = 'changes/%s/detail' % change
  if o_params:
    path += '?%s' % '&'.join(['o=%s' % p for p in o_params])
  return _SendGerritJsonRequest(host, path)


def GetChangeCommit(host, change, revision='current'):
  """Query a Gerrit server for a revision associated with a change."""
  path = 'changes/%s/revisions/%s/commit?links' % (change, revision)
  return _SendGerritJsonRequest(host, path)


def GetChangeCurrentRevision(host, change):
  """Get information about the latest revision for a given change."""
  return QueryChanges(host, [], change, o_params=('CURRENT_REVISION',))


def GetChangeRevisions(host, change):
  """Gets information about all revisions associated with a change."""
  return QueryChanges(host, [], change, o_params=('ALL_REVISIONS',))


def GetChangeReview(host, change, revision=None):
  """Gets the current review information for a change."""
  if not revision:
    jmsg = GetChangeRevisions(host, change)
    if not jmsg:
      return None
    elif len(jmsg) > 1:
      raise GerritError(200, 'Multiple changes found for ChangeId %s.' % change)
    revision = jmsg[0]['current_revision']
  path = 'changes/%s/revisions/%s/review'
  return _SendGerritJsonRequest(host, path)


def GetChangeComments(host, change):
  """Get the line- and file-level comments on a change."""
  path = 'changes/%s/comments' % change
  return _SendGerritJsonRequest(host, path)


def GetRelatedChanges(host: str, change: str, revision: str = 'current') -> Any:
  """Gets information about changes related to a given change."""
  path = 'changes/%s/revisions/%s/related' % (change, revision)
  return _SendGerritJsonRequest(host, path)


def SubmitChange(host, change, wait_for_merge=True):
  """Submits a Gerrit change via Gerrit."""
  path = 'changes/%s/submit' % change
  body = {'wait_for_merge': wait_for_merge}
  conn = CreateHttpConn(host, path, reqtype='POST', body=body)
  return ReadHttpJsonResponse(conn)


def SetReview(
    host: str,
    change: str,
    msg: Optional[str] = None,
    labels: Optional[Dict[str, Any]] = None,
    notify: bool = False,
    ready: bool = False
) -> None:
  """Sets labels and/or adds a message to a code review."""
  if not msg and not labels:
    return
  path = 'changes/%s/revisions/current/review' % change
  body: Dict[str, Any] = {'drafts': 'KEEP'}
  if msg:
    body['message'] = msg
  if labels:
    body['labels'] = labels
  if notify:
    body['notify'] = 'ALL' if notify else 'NONE'
  if ready:
    body['ready'] = True
  response = _SendGerritJsonRequest(host, path, reqtype='POST', body=body)
  if response is None:
      raise GerritError(200, 'No response from Gerrit.')
  if labels:
    for key, val in labels.items():
      if ('labels' not in response or key not in response['labels'] or
          int(response['labels'][key] != int(val))):
        raise GerritError(200, 'Unable to set "%s" label on change %s.' % (
            key, change))


def GetReviewers(host, change):
  """Gets information about all reviewers attached to a change."""
  path = 'changes/%s/reviewers' % change
  return _SendGerritJsonRequest(host, path)


def GetReview(host, change, revision):
  """Gets review information about a specific revision of a change."""
  path = 'changes/%s/revisions/%s/review' % (change, revision)
  return _SendGerritJsonRequest(host, path)


def ChangeIdentifier(project, change_number):
  """Returns change identifier "project~number" suitable for |change| arg of
  this module API.

  Such format is allows for more efficient Gerrit routing of HTTP requests,
  comparing to specifying just change_number.
  """
  assert int(change_number)
  return '%s~%s' % (urllib.parse.quote(project, ''), change_number)
