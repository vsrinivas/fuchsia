class FailFastException implements Exception {
  @override
  String toString() => 'FailFastException';
}

class MalformedFuchsiaUrlException implements Exception {
  final String packageUrl;
  MalformedFuchsiaUrlException(this.packageUrl);

  @override
  String toString() =>
      'MalformedFuchsiaUrlException: Malformed Fuchsia Package '
      'Url `$packageUrl` could not be parsed';
}

class UnparsedTestException implements Exception {
  final String message;
  UnparsedTestException(this.message);

  @override
  String toString() => message;
}
