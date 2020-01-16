class BuildException implements Exception {
  final String buildHandle;
  final int exitCode;
  BuildException(this.buildHandle, [this.exitCode = 2]);
  @override
  String toString() => 'BuildException: Failed to run `$buildHandle` :: '
      'Exit Code: $exitCode';
}

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

class UnrunnableTestException implements Exception {
  final String message;
  UnrunnableTestException(this.message);

  @override
  String toString() => message;
}

class SigIntException implements Exception {}
