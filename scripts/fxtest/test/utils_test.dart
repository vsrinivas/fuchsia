import 'package:test/test.dart';
import 'package:fxtest/fxtest.dart';

void main() {
  group('fuchsia-package URLs are correctly parsed', () {
    test('when all components are present', () {
      PackageUrl packageUrl = PackageUrl.fromString(
          'fuchsia-pkg://myroot.com/pkg-name/VARIANT?hash=asdf#OMG.cmx');
      expect(packageUrl.host, 'myroot.com');
      expect(packageUrl.packageName, 'pkg-name');
      expect(packageUrl.packageVariant, 'VARIANT');
      expect(packageUrl.hash, 'asdf');
      expect(packageUrl.resourcePath, 'OMG.cmx');
      expect(packageUrl.rawResource, 'OMG');
    });

    test('when the variant is missing', () {
      PackageUrl packageUrl = PackageUrl.fromString(
          'fuchsia-pkg://myroot.com/pkg-name?hash=asdf#OMG.cmx');
      expect(packageUrl.host, 'myroot.com');
      expect(packageUrl.packageName, 'pkg-name');
      expect(packageUrl.packageVariant, null);
      expect(packageUrl.hash, 'asdf');
      expect(packageUrl.resourcePath, 'OMG.cmx');
      expect(packageUrl.rawResource, 'OMG');
    });

    test('when the hash is missing', () {
      PackageUrl packageUrl = PackageUrl.fromString(
          'fuchsia-pkg://myroot.com/pkg-name/VARIANT#OMG.cmx');
      expect(packageUrl.host, 'myroot.com');
      expect(packageUrl.packageName, 'pkg-name');
      expect(packageUrl.packageVariant, 'VARIANT');
      expect(packageUrl.hash, null);
      expect(packageUrl.resourcePath, 'OMG.cmx');
      expect(packageUrl.rawResource, 'OMG');
    });

    test('when the resource path is missing', () {
      PackageUrl packageUrl = PackageUrl.fromString(
          'fuchsia-pkg://myroot.com/pkg-name/VARIANT?hash=asdf');
      expect(packageUrl.host, 'myroot.com');
      expect(packageUrl.packageName, 'pkg-name');
      expect(packageUrl.packageVariant, 'VARIANT');
      expect(packageUrl.hash, 'asdf');
      expect(packageUrl.resourcePath, '');
      expect(packageUrl.rawResource, '');
    });

    test('when the variant and hash are missing', () {
      PackageUrl packageUrl =
          PackageUrl.fromString('fuchsia-pkg://myroot.com/pkg-name#OMG.cmx');
      expect(packageUrl.host, 'myroot.com');
      expect(packageUrl.packageName, 'pkg-name');
      expect(packageUrl.packageVariant, null);
      expect(packageUrl.hash, null);
      expect(packageUrl.resourcePath, 'OMG.cmx');
      expect(packageUrl.rawResource, 'OMG');
    });
    test('when the variant and resource path are missing', () {
      PackageUrl packageUrl =
          PackageUrl.fromString('fuchsia-pkg://myroot.com/pkg-name?hash=asdf');
      expect(packageUrl.host, 'myroot.com');
      expect(packageUrl.packageName, 'pkg-name');
      expect(packageUrl.packageVariant, null);
      expect(packageUrl.hash, 'asdf');
      expect(packageUrl.resourcePath, '');
      expect(packageUrl.rawResource, '');
    });

    test('when the hash and resource path are missing', () {
      PackageUrl packageUrl =
          PackageUrl.fromString('fuchsia-pkg://myroot.com/pkg-name/VARIANT');
      expect(packageUrl.host, 'myroot.com');
      expect(packageUrl.packageName, 'pkg-name');
      expect(packageUrl.packageVariant, 'VARIANT');
      expect(packageUrl.hash, null);
      expect(packageUrl.resourcePath, '');
      expect(packageUrl.rawResource, '');
    });

    test('when the variant, hash, and resource path are missing', () {
      PackageUrl packageUrl =
          PackageUrl.fromString('fuchsia-pkg://myroot.com/pkg-name');
      expect(packageUrl.host, 'myroot.com');
      expect(packageUrl.packageName, 'pkg-name');
      expect(packageUrl.packageVariant, null);
      expect(packageUrl.hash, null);
      expect(packageUrl.resourcePath, '');
      expect(packageUrl.rawResource, '');
    });
  });
}
