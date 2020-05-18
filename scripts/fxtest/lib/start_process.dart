import 'dart:io';

/// A function that starts a process when called.
///
/// In most usages, this should just be a tearoff of [Process.start]. This can
/// be replaced in tests to prevent actual creation of system processes.
typedef StartProcess = Future<Process> Function(
    String executable, List<String> arguments,
    {String workingDirectory,
    Map<String, String> environment,
    bool includeParentEnvironment,
    bool runInShell,
    ProcessStartMode mode});
