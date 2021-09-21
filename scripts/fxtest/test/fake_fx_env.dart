// TODO(https://fxbug.dev/84961): Fix null safety and remove this language version.
// @dart=2.9
import 'package:fxutils/fxutils.dart';
import 'package:path/path.dart' as p;

/// Convenience class for tests which returns values vaguely of the same
/// structure as real values, but never suitable for any actual work. This can
/// only safely be used by tests making sure they *would* do work correctly,
/// without having to repeatedly instantiate the same fakes over and over.
class FakeFxEnv extends IFxEnv {
  final String _cwd;
  final String _fuchsiaDir;
  final EnvReader _envReader;

  FakeFxEnv({
    String fuchsiaDir = '/root/fuchsia',
    String cwd = '/cwd',
    EnvReader envReader,
  })  : _envReader = envReader,
        _fuchsiaDir = fuchsiaDir,
        _cwd = cwd;

  static final FakeFxEnv shared = FakeFxEnv();

  @override
  String get cwd => _cwd;

  @override
  String get fuchsiaArch => 'x86';

  @override
  String get fuchsiaDir => _fuchsiaDir;

  @override
  String get hostOutDir => p.join(fuchsiaDir, 'out/default/host_$fuchsiaArch');

  @override
  String get outputDir => p.join(fuchsiaDir, 'out/default');

  @override
  String get sshKey => p.join(fuchsiaDir, 'out/default/.ssh/pkey');

  @override
  String getEnv(String variableName, [String defaultValue]) =>
      _envReader.getEnv(variableName, defaultValue);
}
