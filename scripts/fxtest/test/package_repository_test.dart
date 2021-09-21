// TODO(https://fxbug.dev/84961): Fix null safety and remove this language version.
// @dart=2.9
import 'package:fxtest/fxtest.dart';
import 'package:test/test.dart';

void main() {
  group('reads package repository information correctly', () {
    test('parsing package-repositories.json', () {
      List<dynamic> manifestTestJson = [
        {
          'blobs': 'amber-files/repository/blobs',
          'path': 'amber-files/repository',
          'targets': 'amber-files/repository/targets.json'
        }
      ];
      PackageRepository repository =
          PackageRepository.fromJson(manifestTestJson);
      expect(repository.targetsFile, 'amber-files/repository/targets.json');
      expect(repository.blobsDirectory, 'amber-files/repository/blobs');
      expect(repository.rootPath, 'amber-files/repository');
    });

    test(
        'refuse to parse a package-repositories.json with multiple repositories',
        () {
      List<dynamic> manifestTestJson = [
        {
          'blobs': 'amber-files/repository/blobs',
          'path': 'amber-files/repository',
          'targets': 'amber-files/repository/targets.json'
        },
        {
          'blobs': 'amber-files2/repository/blobs',
          'path': 'amber-files2/repository',
          'targets': 'amber-files2/repository/targets.json'
        }
      ];
      expect(() => PackageRepository.fromJson(manifestTestJson),
          throwsA(TypeMatcher<PackageRepositoryException>()));
    });

    test('parsing targets.json', () async {
      // This is adapted from an actual targets.json, intentially keeping the unused
      // fields to ensure they don't impact parsing.
      Map<String, dynamic> targetsJson = {
        'signatures': [
          {
            'keyid':
                'ffffffffff69aa5eb30d6c4be6b9170737813be937206345c4b2e19ffffffffff',
            'method': 'ed25519',
            'sig':
                'ffffffffffbbbec5c8840dd15f0fba8fdda835e98bbcb91db7a6ffff95ea3f03f5697b654c8133ec86045bb21f8f907c959de494ea11319bec5f90ffffffffff'
          }
        ],
        'signed': {
          '_type': 'targets',
          'expires': '2020-08-13T00:19:56Z',
          'spec_version': '1.0',
          'targets': {
            'my-demo/0': {
              'custom': {
                'merkle':
                    '913cdd63ab4aa794694448450505efaa2a8fe27fb33888e5156da9db60ac0a29',
                'size': 16384
              },
              'hashes': {
                'sha512':
                    '2a5294cc86c41df4651e9168ea8d526edd732fa4e7e329abfe5070228b61319c66f56902517d4a1edf5293e21523f669ff0211f51f3e696c4deb3d678edb7f00'
              },
              'length': 16384
            },
            'my_lib/0': {
              'custom': {
                'merkle':
                    '7a604498e05fa012391b6b51da9cc74ff6a6a9d25b1376de98125c194232bfa1',
                'size': 61440
              },
              'hashes': {
                'sha512':
                    '93430eb8b66cc507d3c36f4a15fc5d7ba3e25d0a1569dddad46355762386a606e90798ec429d35b3defc2df7a67408fa8e4e253d04042624c18e7ce22659a314'
              },
              'length': 61440
            }
          }
        }
      };
      PackageRepository repository = PackageRepository('does', 'not', 'matter');
      await repository.loadTargetsFromJson(Stream.value(targetsJson));

      expect(repository.asMap().length, 2);
      expect(repository['my-demo'].merkle,
          '913cdd63ab4aa794694448450505efaa2a8fe27fb33888e5156da9db60ac0a29');
      expect(repository['my_lib'].merkle,
          '7a604498e05fa012391b6b51da9cc74ff6a6a9d25b1376de98125c194232bfa1');
    });

    test('parsing targets.json with multiple versions of the same package name',
        () async {
      Map<String, dynamic> targetsJson = {
        'signed': {
          'targets': {
            'my-demo/0': {
              'custom': {
                'merkle':
                    '913cdd63ab4aa794694448450505efaa2a8fe27fb33888e5156da9db60ac0a29'
              }
            },
            'my-demo/1': {
              'custom': {
                'merkle':
                    '7a604498e05fa012391b6b51da9cc74ff6a6a9d25b1376de98125c194232bfa1'
              }
            }
          }
        }
      };
      PackageRepository repository = PackageRepository('does', 'not', 'matter');
      await repository.loadTargetsFromJson(Stream.value(targetsJson));

      expect(repository.asMap().length, 1);
      expect(repository['my-demo']['0'],
          '913cdd63ab4aa794694448450505efaa2a8fe27fb33888e5156da9db60ac0a29');
      expect(repository['my-demo']['1'],
          '7a604498e05fa012391b6b51da9cc74ff6a6a9d25b1376de98125c194232bfa1');
      expect(repository['another-package'], isNull);

      // merkle getter should throw an exception when there are multiple versions,
      // since it cannot reliably decide which version to return.
      expect(() => repository['my-demo'].merkle,
          throwsA(TypeMatcher<PackageRepositoryException>()));
    });
  });
}
