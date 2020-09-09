// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::Error;

use {
    super::Algorithm,
    crypto::{rc4, symmetriccipher::SynchronousStreamCipher},
};

#[allow(unused, dead_code)]
pub struct Rc4;

impl Rc4 {
    fn process_with_skip(key: &[u8], data: &[u8], skip: usize) -> Vec<u8> {
        let mut rc4 = rc4::Rc4::new(&key[..]);
        let mut out = vec![0u8; data.len()];
        rc4.process(&vec![0u8; skip][..], &mut vec![0u8; skip][..]);
        rc4.process(data, &mut out);
        out
    }
}

const STREAM_SKIP: usize = 256;

impl Algorithm for Rc4 {
    fn wrap_key(&self, kek: &[u8], iv: &[u8; 16], data: &[u8]) -> Result<Vec<u8>, Error> {
        // The key construction and skip used here are not obviously documented by WFA, and were
        // determined from the wpa_supplicant implementation.
        // RC4 uses a combination of the KEK and IV as an encryption key.
        let mut key = kek.to_vec();
        key.extend(iv.iter().cloned());
        // RC4 advances the stream cipher before beginning encryption.
        Ok(Self::process_with_skip(&key[..], data, STREAM_SKIP))
    }

    fn unwrap_key(&self, kek: &[u8], iv: &[u8; 16], data: &[u8]) -> Result<Vec<u8>, Error> {
        // RC4 is a symmetric cipher.
        self.wrap_key(kek, iv, data)
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use hex::FromHex;

    #[test]
    fn wrap_unwrap() {
        let kek = Vec::from_hex("ea75").unwrap();
        let iv = [0xab; 16];
        let data = Vec::from_hex("11112222abba").unwrap();
        let rc4 = Rc4;
        let wrapped = rc4.wrap_key(&kek[..], &iv, &data[..]).unwrap();
        let unwrapped = rc4.unwrap_key(&kek[..], &iv, &wrapped[..]).unwrap();
        assert_eq!(unwrapped, data);
    }

    // There are no published test vectors for the specific usage of RC4 we have here. We instead
    // use RFC 6229 test vectors to validate that the underlying third party implementation of RC4
    // produces correct byte streams.
    fn test_stream<T: AsRef<[u8]>>(key_hex: T, expected_stream: T, skip: usize) {
        let key = Vec::from_hex(key_hex).unwrap();
        let expected = Vec::from_hex(expected_stream).unwrap();
        let actual = Rc4::process_with_skip(&key[..], &vec![0u8; expected.len()][..], skip);
        assert_eq!(expected, actual);
    }

    #[test]
    fn test_key_generation() {
        // Based on RFC 6229 192 bit key 1 test case
        let kek = [0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08];
        let iv = [
            0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16,
            0x17, 0x18,
        ];
        let expected = Vec::from_hex("6bd2378ec341c9a42f37ba79f88a32ff").unwrap();
        let data = vec![0u8; expected.len()];
        let rc4 = Rc4;
        let wrapped = rc4.wrap_key(&kek[..], &iv, &data[..]).unwrap();
        assert_eq!(wrapped, expected);
    }

    #[test]
    fn test_40_bit_key_1() {
        let key = "0102030405";
        test_stream(key, "b2396305f03dc027ccc3524a0a1118a8", 0x0000);
        test_stream(key, "6982944f18fc82d589c403a47a0d0919", 0x0010);
        test_stream(key, "28cb1132c96ce286421dcaadb8b69eae", 0x00f0);
        test_stream(key, "1cfcf62b03eddb641d77dfcf7f8d8c93", 0x0100);
        test_stream(key, "42b7d0cdd918a8a33dd51781c81f4041", 0x01f0);
        test_stream(key, "6459844432a7da923cfb3eb4980661f6", 0x0200);
        test_stream(key, "ec10327bde2beefd18f9277680457e22", 0x02f0);
        test_stream(key, "eb62638d4f0ba1fe9fca20e05bf8ff2b", 0x0300);
        test_stream(key, "45129048e6a0ed0b56b490338f078da5", 0x03f0);
        test_stream(key, "30abbcc7c20b01609f23ee2d5f6bb7df", 0x0400);
        test_stream(key, "3294f744d8f9790507e70f62e5bbceea", 0x05f0);
        test_stream(key, "d8729db41882259bee4f825325f5a130", 0x0600);
        test_stream(key, "1eb14a0c13b3bf47fa2a0ba93ad45b8b", 0x07f0);
        test_stream(key, "cc582f8ba9f265e2b1be9112e975d2d7", 0x0800);
        test_stream(key, "f2e30f9bd102ecbf75aaade9bc35c43c", 0x0bf0);
        test_stream(key, "ec0e11c479dc329dc8da7968fe965681", 0x0c00);
        test_stream(key, "068326a2118416d21f9d04b2cd1ca050", 0x0ff0);
        test_stream(key, "ff25b58995996707e51fbdf08b34d875", 0x1000);
    }

    #[test]
    fn test_56_bit_key_1() {
        let key = "01020304050607";
        test_stream(key, "293f02d47f37c9b633f2af5285feb46b", 0x0000);
        test_stream(key, "e620f1390d19bd84e2e0fd752031afc1", 0x0010);
        test_stream(key, "914f02531c9218810df60f67e338154c", 0x00f0);
        test_stream(key, "d0fdb583073ce85ab83917740ec011d5", 0x0100);
        test_stream(key, "75f81411e871cffa70b90c74c592e454", 0x01f0);
        test_stream(key, "0bb87202938dad609e87a5a1b079e5e4", 0x0200);
        test_stream(key, "c2911246b612e7e7b903dfeda1dad866", 0x02f0);
        test_stream(key, "32828f91502b6291368de8081de36fc2", 0x0300);
        test_stream(key, "f3b9a7e3b297bf9ad804512f9063eff1", 0x03f0);
        test_stream(key, "8ecb67a9ba1f55a5a067e2b026a3676f", 0x0400);
        test_stream(key, "d2aa902bd42d0d7cfd340cd45810529f", 0x05f0);
        test_stream(key, "78b272c96e42eab4c60bd914e39d06e3", 0x0600);
        test_stream(key, "f4332fd31a079396ee3cee3f2a4ff049", 0x07f0);
        test_stream(key, "05459781d41fda7f30c1be7e1246c623", 0x0800);
        test_stream(key, "adfd3868b8e51485d5e610017e3dd609", 0x0bf0);
        test_stream(key, "ad26581c0c5be45f4cea01db2f3805d5", 0x0c00);
        test_stream(key, "f3172ceffc3b3d997c85ccd5af1a950c", 0x0ff0);
        test_stream(key, "e74b0b9731227fd37c0ec08a47ddd8b8", 0x1000);
    }

    #[test]
    fn test_64_bit_key_1() {
        let key = "0102030405060708";
        test_stream(key, "97ab8a1bf0afb96132f2f67258da15a8", 0x0000);
        test_stream(key, "8263efdb45c4a18684ef87e6b19e5b09", 0x0010);
        test_stream(key, "9636ebc9841926f4f7d1f362bddf6e18", 0x00f0);
        test_stream(key, "d0a990ff2c05fef5b90373c9ff4b870a", 0x0100);
        test_stream(key, "73239f1db7f41d80b643c0c52518ec63", 0x01f0);
        test_stream(key, "163b319923a6bdb4527c626126703c0f", 0x0200);
        test_stream(key, "49d6c8af0f97144a87df21d91472f966", 0x02f0);
        test_stream(key, "44173a103b6616c5d5ad1cee40c863d0", 0x0300);
        test_stream(key, "273c9c4b27f322e4e716ef53a47de7a4", 0x03f0);
        test_stream(key, "c6d0e7b226259fa9023490b26167ad1d", 0x0400);
        test_stream(key, "1fe8986713f07c3d9ae1c163ff8cf9d3", 0x05f0);
        test_stream(key, "8369e1a965610be887fbd0c79162aafb", 0x0600);
        test_stream(key, "0a0127abb44484b9fbef5abcae1b579f", 0x07f0);
        test_stream(key, "c2cdadc6402e8ee866e1f37bdb47e42c", 0x0800);
        test_stream(key, "26b51ea37df8e1d6f76fc3b66a7429b3", 0x0bf0);
        test_stream(key, "bc7683205d4f443dc1f29dda3315c87b", 0x0c00);
        test_stream(key, "d5fa5a3469d29aaaf83d23589db8c85b", 0x0ff0);
        test_stream(key, "3fb46e2c8f0f068edce8cdcd7dfc5862", 0x1000);
    }

    #[test]
    fn test_80_bit_key_1() {
        let key = "0102030405060708090a";
        test_stream(key, "ede3b04643e586cc907dc21851709902", 0x0000);
        test_stream(key, "03516ba78f413beb223aa5d4d2df6711", 0x0010);
        test_stream(key, "3cfd6cb58ee0fdde640176ad0000044d", 0x00f0);
        test_stream(key, "48532b21fb6079c9114c0ffd9c04a1ad", 0x0100);
        test_stream(key, "3e8cea98017109979084b1ef92f99d86", 0x01f0);
        test_stream(key, "e20fb49bdb337ee48b8d8dc0f4afeffe", 0x0200);
        test_stream(key, "5c2521eacd7966f15e056544bea0d315", 0x02f0);
        test_stream(key, "e067a7031931a246a6c3875d2f678acb", 0x0300);
        test_stream(key, "a64f70af88ae56b6f87581c0e23e6b08", 0x03f0);
        test_stream(key, "f449031de312814ec6f319291f4a0516", 0x0400);
        test_stream(key, "bdae85924b3cb1d0a2e33a30c6d79599", 0x05f0);
        test_stream(key, "8a0feddbac865a09bcd127fb562ed60a", 0x0600);
        test_stream(key, "b55a0a5b51a12a8be34899c3e047511a", 0x07f0);
        test_stream(key, "d9a09cea3ce75fe39698070317a71339", 0x0800);
        test_stream(key, "552225ed1177f44584ac8cfa6c4eb5fc", 0x0bf0);
        test_stream(key, "7e82cbabfc95381b080998442129c2f8", 0x0c00);
        test_stream(key, "1f135ed14ce60a91369d2322bef25e3c", 0x0ff0);
        test_stream(key, "08b6be45124a43e2eb77953f84dc8553", 0x1000);
    }

    #[test]
    fn test_128_bit_key_1() {
        let key = "0102030405060708090a0b0c0d0e0f10";
        test_stream(key, "9ac7cc9a609d1ef7b2932899cde41b97", 0x0000);
        test_stream(key, "5248c4959014126a6e8a84f11d1a9e1c", 0x0010);
        test_stream(key, "065902e4b620f6cc36c8589f66432f2b", 0x00f0);
        test_stream(key, "d39d566bc6bce3010768151549f3873f", 0x0100);
        test_stream(key, "b6d1e6c4a5e4771cad79538df295fb11", 0x01f0);
        test_stream(key, "c68c1d5c559a974123df1dbc52a43b89", 0x0200);
        test_stream(key, "c5ecf88de897fd57fed301701b82a259", 0x02f0);
        test_stream(key, "eccbe13de1fcc91c11a0b26c0bc8fa4d", 0x0300);
        test_stream(key, "e7a72574f8782ae26aabcf9ebcd66065", 0x03f0);
        test_stream(key, "bdf0324e6083dcc6d3cedd3ca8c53c16", 0x0400);
        test_stream(key, "b40110c4190b5622a96116b0017ed297", 0x05f0);
        test_stream(key, "ffa0b514647ec04f6306b892ae661181", 0x0600);
        test_stream(key, "d03d1bc03cd33d70dff9fa5d71963ebd", 0x07f0);
        test_stream(key, "8a44126411eaa78bd51e8d87a8879bf5", 0x0800);
        test_stream(key, "fabeb76028ade2d0e48722e46c4615a3", 0x0bf0);
        test_stream(key, "c05d88abd50357f935a63c59ee537623", 0x0c00);
        test_stream(key, "ff38265c1642c1abe8d3c2fe5e572bf8", 0x0ff0);
        test_stream(key, "a36a4c301ae8ac13610ccbc12256cacc", 0x1000);
    }

    #[test]
    fn test_192_bit_key_1() {
        let key = "0102030405060708090a0b0c0d0e0f101112131415161718";
        test_stream(key, "0595e57fe5f0bb3c706edac8a4b2db11", 0x0000);
        test_stream(key, "dfde31344a1af769c74f070aee9e2326", 0x0010);
        test_stream(key, "b06b9b1e195d13d8f4a7995c4553ac05", 0x00f0);
        test_stream(key, "6bd2378ec341c9a42f37ba79f88a32ff", 0x0100);
        test_stream(key, "e70bce1df7645adb5d2c4130215c3522", 0x01f0);
        test_stream(key, "9a5730c7fcb4c9af51ffda89c7f1ad22", 0x0200);
        test_stream(key, "0485055fd4f6f0d963ef5ab9a5476982", 0x02f0);
        test_stream(key, "591fc66bcda10e452b03d4551f6b62ac", 0x0300);
        test_stream(key, "2753cc83988afa3e1688a1d3b42c9a02", 0x03f0);
        test_stream(key, "93610d523d1d3f0062b3c2a3bbc7c7f0", 0x0400);
        test_stream(key, "96c248610aadedfeaf8978c03de8205a", 0x05f0);
        test_stream(key, "0e317b3d1c73b9e9a4688f296d133a19", 0x0600);
        test_stream(key, "bdf0e6c3cca5b5b9d533b69c56ada120", 0x07f0);
        test_stream(key, "88a218b6e2ece1e6246d44c759d19b10", 0x0800);
        test_stream(key, "6866397e95c140534f94263421006e40", 0x0bf0);
        test_stream(key, "32cb0a1e9542c6b3b8b398abc3b0f1d5", 0x0c00);
        test_stream(key, "29a0b8aed54a132324c62e423f54b4c8", 0x0ff0);
        test_stream(key, "3cb0f3b5020a98b82af9fe154484a168", 0x1000);
    }

    #[test]
    fn test_256_bit_key_1() {
        let key = "0102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f20";
        test_stream(key, "eaa6bd25880bf93d3f5d1e4ca2611d91", 0x0000);
        test_stream(key, "cfa45c9f7e714b54bdfa80027cb14380", 0x0010);
        test_stream(key, "114ae344ded71b35f2e60febad727fd8", 0x00f0);
        test_stream(key, "02e1e7056b0f623900496422943e97b6", 0x0100);
        test_stream(key, "91cb93c787964e10d9527d999c6f936b", 0x01f0);
        test_stream(key, "49b18b42f8e8367cbeb5ef104ba1c7cd", 0x0200);
        test_stream(key, "87084b3ba700bade955610672745b374", 0x02f0);
        test_stream(key, "e7a7b9e9ec540d5ff43bdb12792d1b35", 0x0300);
        test_stream(key, "c799b596738f6b018c76c74b1759bd90", 0x03f0);
        test_stream(key, "7fec5bfd9f9b89ce6548309092d7e958", 0x0400);
        test_stream(key, "40f250b26d1f096a4afd4c340a588815", 0x05f0);
        test_stream(key, "3e34135c79db010200767651cf263073", 0x0600);
        test_stream(key, "f656abccf88dd827027b2ce917d464ec", 0x07f0);
        test_stream(key, "18b62503bfbc077fbabb98f20d98ab34", 0x0800);
        test_stream(key, "8aed95ee5b0dcbfbef4eb21d3a3f52f9", 0x0bf0);
        test_stream(key, "625a1ab00ee39a5327346bddb01a9c18", 0x0c00);
        test_stream(key, "a13a7c79c7e119b5ab0296ab28c300b9", 0x0ff0);
        test_stream(key, "f3e4c0a2e02d1d01f7f0a74618af2b48", 0x1000);
    }

    #[test]
    fn test_40_bit_key_2() {
        let key = "833222772a";
        test_stream(key, "80ad97bdc973df8a2e879e92a497efda", 0x0000);
        test_stream(key, "20f060c2f2e5126501d3d4fea10d5fc0", 0x0010);
        test_stream(key, "faa148e99046181fec6b2085f3b20ed9", 0x00f0);
        test_stream(key, "f0daf5bab3d596839857846f73fbfe5a", 0x0100);
        test_stream(key, "1c7e2fc4639232fe297584b296996bc8", 0x01f0);
        test_stream(key, "3db9b249406cc8edffac55ccd322ba12", 0x0200);
        test_stream(key, "e4f9f7e0066154bbd125b745569bc897", 0x02f0);
        test_stream(key, "75d5ef262b44c41a9cf63ae14568e1b9", 0x0300);
        test_stream(key, "6da453dbf81e82334a3d8866cb50a1e3", 0x03f0);
        test_stream(key, "7828d074119cab5c22b294d7a9bfa0bb", 0x0400);
        test_stream(key, "adb89cea9a15fbe617295bd04b8ca05c", 0x05f0);
        test_stream(key, "6251d87fd4aaae9a7e4ad5c217d3f300", 0x0600);
        test_stream(key, "e7119bd6dd9b22afe8f89585432881e2", 0x07f0);
        test_stream(key, "785b60fd7ec4e9fcb6545f350d660fab", 0x0800);
        test_stream(key, "afecc037fdb7b0838eb3d70bcd268382", 0x0bf0);
        test_stream(key, "dbc1a7b49d57358cc9fa6d61d73b7cf0", 0x0c00);
        test_stream(key, "6349d126a37afcba89794f9804914fdc", 0x0ff0);
        test_stream(key, "bf42c3018c2f7c66bfde524975768115", 0x1000);
    }

    #[test]
    fn test_56_bit_key_2() {
        let key = "1910833222772a";
        test_stream(key, "bc9222dbd3274d8fc66d14ccbda6690b", 0x0000);
        test_stream(key, "7ae627410c9a2be693df5bb7485a63e3", 0x0010);
        test_stream(key, "3f0931aa03defb300f060103826f2a64", 0x00f0);
        test_stream(key, "beaa9ec8d59bb68129f3027c96361181", 0x0100);
        test_stream(key, "74e04db46d28648d7dee8a0064b06cfe", 0x01f0);
        test_stream(key, "9b5e81c62fe023c55be42f87bbf932b8", 0x0200);
        test_stream(key, "ce178fc1826efecbc182f57999a46140", 0x02f0);
        test_stream(key, "8bdf55cd55061c06dba6be11de4a578a", 0x0300);
        test_stream(key, "626f5f4dce652501f3087d39c92cc349", 0x03f0);
        test_stream(key, "42daac6a8f9ab9a7fd137c6037825682", 0x0400);
        test_stream(key, "cc03fdb79192a207312f53f5d4dc33d9", 0x05f0);
        test_stream(key, "f70f14122a1c98a3155d28b8a0a8a41d", 0x0600);
        test_stream(key, "2a3a307ab2708a9c00fe0b42f9c2d6a1", 0x07f0);
        test_stream(key, "862617627d2261eab0b1246597ca0ae9", 0x0800);
        test_stream(key, "55f877ce4f2e1ddbbf8e13e2cde0fdc8", 0x0bf0);
        test_stream(key, "1b1556cb935f173337705fbb5d501fc1", 0x0c00);
        test_stream(key, "ecd0e96602be7f8d5092816cccf2c2e9", 0x0ff0);
        test_stream(key, "027881fab4993a1c262024a94fff3f61", 0x1000);
    }

    #[test]
    fn test_64_bit_key_2() {
        let key = "641910833222772a";
        test_stream(key, "bbf609de9413172d07660cb680716926", 0x0000);
        test_stream(key, "46101a6dab43115d6c522b4fe93604a9", 0x0010);
        test_stream(key, "cbe1fff21c96f3eef61e8fe0542cbdf0", 0x00f0);
        test_stream(key, "347938bffa4009c512cfb4034b0dd1a7", 0x0100);
        test_stream(key, "7867a786d00a7147904d76ddf1e520e3", 0x01f0);
        test_stream(key, "8d3e9e1caefcccb3fbf8d18f64120b32", 0x0200);
        test_stream(key, "942337f8fd76f0fae8c52d7954810672", 0x02f0);
        test_stream(key, "b8548c10f51667f6e60e182fa19b30f7", 0x0300);
        test_stream(key, "0211c7c6190c9efd1237c34c8f2e06c4", 0x03f0);
        test_stream(key, "bda64f65276d2aacb8f90212203a808e", 0x0400);
        test_stream(key, "bd3820f732ffb53ec193e79d33e27c73", 0x05f0);
        test_stream(key, "d0168616861907d482e36cdac8cf5749", 0x0600);
        test_stream(key, "97b0f0f224b2d2317114808fb03af7a0", 0x07f0);
        test_stream(key, "e59616e469787939a063ceea9af956d1", 0x0800);
        test_stream(key, "c47e0dc1660919c11101208f9e69aa1f", 0x0bf0);
        test_stream(key, "5ae4f12896b8379a2aad89b5b553d6b0", 0x0c00);
        test_stream(key, "6b6b098d0c293bc2993d80bf0518b6d9", 0x0ff0);
        test_stream(key, "8170cc3ccd92a698621b939dd38fe7b9", 0x1000);
    }

    #[test]
    fn test_80_bit_key_2() {
        let key = "8b37641910833222772a";
        test_stream(key, "ab65c26eddb287600db2fda10d1e605c", 0x0000);
        test_stream(key, "bb759010c29658f2c72d93a2d16d2930", 0x0010);
        test_stream(key, "b901e8036ed1c383cd3c4c4dd0a6ab05", 0x00f0);
        test_stream(key, "3d25ce4922924c55f064943353d78a6c", 0x0100);
        test_stream(key, "12c1aa44bbf87e75e611f69b2c38f49b", 0x01f0);
        test_stream(key, "28f2b3434b65c09877470044c6ea170d", 0x0200);
        test_stream(key, "bd9ef822de5288196134cf8af7839304", 0x02f0);
        test_stream(key, "67559c23f052158470a296f725735a32", 0x0300);
        test_stream(key, "8bab26fbc2c12b0f13e2ab185eabf241", 0x03f0);
        test_stream(key, "31185a6d696f0cfa9b42808b38e132a2", 0x0400);
        test_stream(key, "564d3dae183c5234c8af1e51061c44b5", 0x05f0);
        test_stream(key, "3c0778a7b5f72d3c23a3135c7d67b9f4", 0x0600);
        test_stream(key, "f34369890fcf16fb517dcaae4463b2dd", 0x07f0);
        test_stream(key, "02f31c81e8200731b899b028e791bfa7", 0x0800);
        test_stream(key, "72da646283228c14300853701795616f", 0x0bf0);
        test_stream(key, "4e0a8c6f7934a788e2265e81d6d0c8f4", 0x0c00);
        test_stream(key, "438dd5eafea0111b6f36b4b938da2a68", 0x0ff0);
        test_stream(key, "5f6bfc73815874d97100f086979357d8", 0x1000);
    }

    #[test]
    fn test_128_bit_key_2() {
        let key = "ebb46227c6cc8b37641910833222772a";
        test_stream(key, "720c94b63edf44e131d950ca211a5a30", 0x0000);
        test_stream(key, "c366fdeacf9ca80436be7c358424d20b", 0x0010);
        test_stream(key, "b3394a40aabf75cba42282ef25a0059f", 0x00f0);
        test_stream(key, "4847d81da4942dbc249defc48c922b9f", 0x0100);
        test_stream(key, "08128c469f275342adda202b2b58da95", 0x01f0);
        test_stream(key, "970dacef40ad98723bac5d6955b81761", 0x0200);
        test_stream(key, "3cb89993b07b0ced93de13d2a11013ac", 0x02f0);
        test_stream(key, "ef2d676f1545c2c13dc680a02f4adbfe", 0x0300);
        test_stream(key, "b60595514f24bc9fe522a6cad7393644", 0x03f0);
        test_stream(key, "b515a8c5011754f59003058bdb81514e", 0x0400);
        test_stream(key, "3c70047e8cbc038e3b9820db601da495", 0x05f0);
        test_stream(key, "1175da6ee756de46a53e2b075660b770", 0x0600);
        test_stream(key, "00a542bba02111cc2c65b38ebdba587e", 0x07f0);
        test_stream(key, "5865fdbb5b48064104e830b380f2aede", 0x0800);
        test_stream(key, "34b21ad2ad44e999db2d7f0863f0d9b6", 0x0bf0);
        test_stream(key, "84a9218fc36e8a5f2ccfbeae53a27d25", 0x0c00);
        test_stream(key, "a2221a11b833ccb498a59540f0545f4a", 0x0ff0);
        test_stream(key, "5bbeb4787d59e5373fdbea6c6f75c29b", 0x1000);
    }

    #[test]
    fn test_192_bit_key_2() {
        let key = "c109163908ebe51debb46227c6cc8b37641910833222772a";
        test_stream(key, "54b64e6b5a20b5e2ec84593dc7989da7", 0x0000);
        test_stream(key, "c135eee237a85465ff97dc03924f45ce", 0x0010);
        test_stream(key, "cfcc922fb4a14ab45d6175aabbf2d201", 0x00f0);
        test_stream(key, "837b87e2a446ad0ef798acd02b94124f", 0x0100);
        test_stream(key, "17a6dbd664926a0636b3f4c37a4f4694", 0x01f0);
        test_stream(key, "4a5f9f26aeeed4d4a25f632d305233d9", 0x0200);
        test_stream(key, "80a3d01ef00c8e9a4209c17f4eeb358c", 0x02f0);
        test_stream(key, "d15e7d5ffaaabc0207bf200a117793a2", 0x0300);
        test_stream(key, "349682bf588eaa52d0aa1560346aeafa", 0x03f0);
        test_stream(key, "f5854cdb76c889e3ad63354e5f7275e3", 0x0400);
        test_stream(key, "532c7ceccb39df3236318405a4b1279c", 0x05f0);
        test_stream(key, "baefe6d9ceb651842260e0d1e05e3b90", 0x0600);
        test_stream(key, "e82d8c6db54e3c633f581c952ba04207", 0x07f0);
        test_stream(key, "4b16e50abd381bd70900a9cd9a62cb23", 0x0800);
        test_stream(key, "3682ee33bd148bd9f58656cd8f30d9fb", 0x0bf0);
        test_stream(key, "1e5a0b8475045d9b20b2628624edfd9e", 0x0c00);
        test_stream(key, "63edd684fb826282fe528f9c0e9237bc", 0x0ff0);
        test_stream(key, "e4dd2e98d6960fae0b43545456743391", 0x1000);
    }

    #[test]
    fn test_256_bit_key_2() {
        let key = "1ada31d5cf688221c109163908ebe51debb46227c6cc8b37641910833222772a";
        test_stream(key, "dd5bcb0018e922d494759d7c395d02d3", 0x0000);
        test_stream(key, "c8446f8f77abf737685353eb89a1c9eb", 0x0010);
        test_stream(key, "af3e30f9c095045938151575c3fb9098", 0x00f0);
        test_stream(key, "f8cb6274db99b80b1d2012a98ed48f0e", 0x0100);
        test_stream(key, "25c3005a1cb85de076259839ab7198ab", 0x01f0);
        test_stream(key, "9dcbc183e8cb994b727b75be3180769c", 0x0200);
        test_stream(key, "a1d3078dfa9169503ed9d4491dee4eb2", 0x02f0);
        test_stream(key, "8514a5495858096f596e4bcd66b10665", 0x0300);
        test_stream(key, "5f40d59ec1b03b33738efa60b2255d31", 0x03f0);
        test_stream(key, "3477c7f764a41baceff90bf14f92b7cc", 0x0400);
        test_stream(key, "ac4e95368d99b9eb78b8da8f81ffa795", 0x05f0);
        test_stream(key, "8c3c13f8c2388bb73f38576e65b7c446", 0x0600);
        test_stream(key, "13c4b9c1dfb66579eddd8a280b9f7316", 0x07f0);
        test_stream(key, "ddd27820550126698efaadc64b64f66e", 0x0800);
        test_stream(key, "f08f2e66d28ed143f3a237cf9de73559", 0x0bf0);
        test_stream(key, "9ea36c525531b880ba124334f57b0b70", 0x0c00);
        test_stream(key, "d5a39e3dfcc50280bac4a6b5aa0dca7d", 0x0ff0);
        test_stream(key, "370b1c1fe655916d97fd0d47ca1d72b8", 0x1000);
    }
}
