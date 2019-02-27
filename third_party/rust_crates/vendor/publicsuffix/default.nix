with import <nixpkgs> {};

stdenv.mkDerivation {
  name = "publicsuffix";
  OPENSSL_DIR = "${openssl.dev}";
  OPENSSL_LIB_DIR = "${openssl.out}/lib";
}
