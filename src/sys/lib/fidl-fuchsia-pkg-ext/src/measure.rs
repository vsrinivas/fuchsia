// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fidl_fuchsia_pkg as fidl;

/// FIDL types that can have their in-line and out-of-line message byte payload size measured.
pub trait Measurable {
    /// Determine the message byte count for this instance.
    fn measure(&self) -> usize;
}

impl Measurable for fidl::BlobId {
    fn measure(&self) -> usize {
        measure_fuchsia_pkg_blob_id::measure(self).num_bytes
    }
}

impl Measurable for fidl::BlobInfo {
    fn measure(&self) -> usize {
        measure_fuchsia_pkg_blob_info::measure(self).num_bytes
    }
}

impl Measurable for fidl::PackageIndexEntry {
    fn measure(&self) -> usize {
        measure_fuchsia_pkg_index_entry::measure(self).num_bytes
    }
}

#[cfg(test)]
mod tests {
    use {super::*, proptest::prelude::*, zerocopy::AsBytes};

    prop_compose! {
        fn arb_package_index_entry()(
            url in ".{0,2048}",
            blob_id: crate::BlobId,
        ) -> fidl::PackageIndexEntry {
            fidl::PackageIndexEntry {
                package_url: fidl::PackageUrl { url, },
                meta_far_blob_id: blob_id.into(),
            }
        }
    }

    proptest! {
        #[test]
        fn blob_id_size_is_as_bytes_size(item: crate::BlobId) {
            let item: fidl::BlobId = item.into();

            let expected = item.as_bytes().len();
            let actual = item.measure();
            prop_assert_eq!(expected, actual);
        }

        #[test]
        fn blob_info_size_is_as_bytes_size(item: crate::BlobInfo) {
            let item: fidl::BlobInfo = item.into();

            let expected = item.as_bytes().len();
            let actual = item.measure();
            prop_assert_eq!(expected, actual);
        }

        #[test]
        fn package_index_entry_size_is_fidl_encoded_size(
            mut item in arb_package_index_entry()
        ) {
            let actual = item.measure();
            let expected = ::fidl::encoding::with_tls_encoded(
                &mut item,
                |bytes, _handles| Result::<_, ::fidl::Error>::Ok(bytes.len())
            ).unwrap();
            prop_assert_eq!(expected, actual);
        }
    }
}
