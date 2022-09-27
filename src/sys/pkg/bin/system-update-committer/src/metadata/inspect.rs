// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    super::errors::{VerifyError, VerifyFailureReason, VerifySource},
    fuchsia_inspect as finspect,
    std::{convert::TryInto, time::Duration},
};

/// Updates inspect state based on the result of the verifications. The inspect hierarchy is
/// constructed in a way that's compatible with Lapis.
pub(super) fn write_to_inspect(
    node: &finspect::Node,
    res: &Result<(), VerifyError>,
    duration: Duration,
) {
    // We need to convert duration to a u64 because that's the largest size integer that inspect
    // can accept. This is safe because duration will be max 1 hour, which fits into u64.
    // For context, 2^64 microseconds is over 5000 centuries.
    let duration_u64 = duration.as_micros().try_into().unwrap_or(u64::MAX);

    match res {
        Ok(()) => node.record_child("ota_verification_duration", |duration_node| {
            duration_node.record_uint("success", duration_u64)
        }),
        Err(error) => {
            let (name, reason) = match error {
                VerifyError::VerifyError(VerifySource::Blobfs, reason) => {
                    ("failure_blobfs", reason)
                }
            };
            node.record_child("ota_verification_duration", |duration_node| {
                duration_node.record_uint(name, duration_u64)
            });
            node.record_child("ota_verification_failure", |reason_node| match reason {
                VerifyFailureReason::Fidl(_) => reason_node.record_uint("blobfs_fidl", 1),
                VerifyFailureReason::Verify(_) => reason_node.record_uint("blobfs_verify", 1),
                VerifyFailureReason::Timeout => reason_node.record_uint("blobfs_timeout", 1),
            });
        }
    };
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        fidl_fuchsia_update_verify as verify,
        fuchsia_inspect::{assert_data_tree, Inspector},
        proptest::prelude::*,
    };

    #[test]
    fn success() {
        let inspector = Inspector::new();

        let () = write_to_inspect(inspector.root(), &Ok(()), Duration::from_micros(2));

        assert_data_tree! {
            inspector,
            root: {
                "ota_verification_duration": {
                    "success" : 2u64,
                }
            }
        }
    }

    #[test]
    fn failure_blobfs_fidl() {
        let inspector = Inspector::new();

        let () = write_to_inspect(
            inspector.root(),
            &Err(VerifyError::VerifyError(
                VerifySource::Blobfs,
                VerifyFailureReason::Fidl(fidl::Error::ExtraBytes),
            )),
            Duration::from_micros(2),
        );

        assert_data_tree! {
            inspector,
            root: {
                "ota_verification_duration": {
                    "failure_blobfs" : 2u64,
                },
                "ota_verification_failure": {
                    "blobfs_fidl": 1u64,
                }
            }
        }
    }

    #[test]
    fn failure_blobfs_timeout() {
        let inspector = Inspector::new();

        let () = write_to_inspect(
            inspector.root(),
            &Err(VerifyError::VerifyError(VerifySource::Blobfs, VerifyFailureReason::Timeout)),
            Duration::from_micros(2),
        );

        assert_data_tree! {
            inspector,
            root: {
                "ota_verification_duration": {
                    "failure_blobfs" : 2u64,
                },
                "ota_verification_failure": {
                    "blobfs_timeout": 1u64,
                }
            }
        }
    }

    #[test]
    fn failure_blobfs_verify() {
        let inspector = Inspector::new();

        let () = write_to_inspect(
            inspector.root(),
            &Err(VerifyError::VerifyError(
                VerifySource::Blobfs,
                VerifyFailureReason::Verify(verify::VerifyError::Internal),
            )),
            Duration::from_micros(2),
        );

        assert_data_tree! {
            inspector,
            root: {
                "ota_verification_duration": {
                    "failure_blobfs" : 2u64,
                },
                "ota_verification_failure": {
                    "blobfs_verify": 1u64,
                }
            }
        }
    }

    proptest! {
         /// Check the largest reported duration is u64::MAX, even if the actual duration is longer.
        #[test]
        fn success_duration_max_u64(nanos in 0u32..1_000_000_000) {
            let inspector = Inspector::new();

            let () =
                write_to_inspect(inspector.root(), &Ok(()), Duration::new(u64::MAX, nanos));

            assert_data_tree! {
                inspector,
                root: {
                    "ota_verification_duration": {
                        "success" : u64::MAX,
                    }
                }
            }
        }
    }
}
