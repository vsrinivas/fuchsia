use assert_matches::assert_matches;
use chrono::offset::Utc;
use futures_executor::block_on;
use tuf::crypto::{Ed25519PrivateKey, HashAlgorithm, PrivateKey};
use tuf::metadata::{
    Delegation, Delegations, MetadataDescription, MetadataPath, TargetPath, TargetsMetadataBuilder,
};
use tuf::pouf::Pouf1;
use tuf::repo_builder::RepoBuilder;
use tuf::repository::EphemeralRepository;
use tuf::Database;
use tuf::Error;

const ED25519_1_PK8: &[u8] = include_bytes!("./ed25519/ed25519-1.pk8.der");
const ED25519_2_PK8: &[u8] = include_bytes!("./ed25519/ed25519-2.pk8.der");
const ED25519_3_PK8: &[u8] = include_bytes!("./ed25519/ed25519-3.pk8.der");
const ED25519_4_PK8: &[u8] = include_bytes!("./ed25519/ed25519-4.pk8.der");
const ED25519_5_PK8: &[u8] = include_bytes!("./ed25519/ed25519-5.pk8.der");
const ED25519_6_PK8: &[u8] = include_bytes!("./ed25519/ed25519-6.pk8.der");

#[test]
fn simple_delegation() {
    block_on(async {
        let now = Utc::now();

        let root_key = Ed25519PrivateKey::from_pkcs8(ED25519_1_PK8).unwrap();
        let snapshot_key = Ed25519PrivateKey::from_pkcs8(ED25519_2_PK8).unwrap();
        let targets_key = Ed25519PrivateKey::from_pkcs8(ED25519_3_PK8).unwrap();
        let timestamp_key = Ed25519PrivateKey::from_pkcs8(ED25519_4_PK8).unwrap();
        let delegation_key = Ed25519PrivateKey::from_pkcs8(ED25519_5_PK8).unwrap();

        let mut repo = EphemeralRepository::new();
        let metadata = RepoBuilder::create(&mut repo)
            .trusted_root_keys(&[&root_key])
            .trusted_snapshot_keys(&[&snapshot_key])
            .trusted_targets_keys(&[&targets_key])
            .trusted_timestamp_keys(&[&timestamp_key])
            .stage_root()
            .unwrap()
            .add_delegation_key(delegation_key.public().clone())
            .add_delegation_role(
                Delegation::builder(MetadataPath::new("delegation").unwrap())
                    .key(delegation_key.public())
                    .delegate_path(TargetPath::new("foo").unwrap())
                    .build()
                    .unwrap(),
            )
            .stage_targets()
            .unwrap()
            .stage_snapshot_with_builder(|builder| {
                builder.insert_metadata_description(
                    MetadataPath::new("delegation").unwrap(),
                    MetadataDescription::from_slice(&[0u8], 1, &[HashAlgorithm::Sha256]).unwrap(),
                )
            })
            .unwrap()
            .commit()
            .await
            .unwrap();

        let mut tuf = Database::<Pouf1>::from_trusted_metadata(&metadata).unwrap();

        //// build the targets ////
        //// build the delegation ////
        let target_file: &[u8] = b"bar";
        let delegation = TargetsMetadataBuilder::new()
            .insert_target_from_slice(
                TargetPath::new("foo").unwrap(),
                target_file,
                &[HashAlgorithm::Sha256],
            )
            .unwrap()
            .signed::<Pouf1>(&delegation_key)
            .unwrap();
        let raw_delegation = delegation.to_raw().unwrap();

        tuf.update_delegated_targets(
            &now,
            &MetadataPath::targets(),
            &MetadataPath::new("delegation").unwrap(),
            &raw_delegation,
        )
        .unwrap();

        assert!(tuf
            .target_description(&TargetPath::new("foo").unwrap())
            .is_ok());
    })
}

#[test]
fn nested_delegation() {
    block_on(async {
        let now = Utc::now();

        let root_key = Ed25519PrivateKey::from_pkcs8(ED25519_1_PK8).unwrap();
        let snapshot_key = Ed25519PrivateKey::from_pkcs8(ED25519_2_PK8).unwrap();
        let targets_key = Ed25519PrivateKey::from_pkcs8(ED25519_3_PK8).unwrap();
        let timestamp_key = Ed25519PrivateKey::from_pkcs8(ED25519_4_PK8).unwrap();
        let delegation_a_key = Ed25519PrivateKey::from_pkcs8(ED25519_5_PK8).unwrap();
        let delegation_b_key = Ed25519PrivateKey::from_pkcs8(ED25519_6_PK8).unwrap();

        let mut repo = EphemeralRepository::new();
        let metadata = RepoBuilder::create(&mut repo)
            .trusted_root_keys(&[&root_key])
            .trusted_snapshot_keys(&[&snapshot_key])
            .trusted_targets_keys(&[&targets_key])
            .trusted_timestamp_keys(&[&timestamp_key])
            .stage_root()
            .unwrap()
            .add_delegation_key(delegation_a_key.public().clone())
            .add_delegation_role(
                Delegation::builder(MetadataPath::new("delegation-a").unwrap())
                    .key(delegation_a_key.public())
                    .delegate_path(TargetPath::new("foo").unwrap())
                    .build()
                    .unwrap(),
            )
            .stage_targets()
            .unwrap()
            .stage_snapshot_with_builder(|builder| {
                builder
                    .insert_metadata_description(
                        MetadataPath::new("delegation-a").unwrap(),
                        MetadataDescription::from_slice(&[0u8], 1, &[HashAlgorithm::Sha256])
                            .unwrap(),
                    )
                    .insert_metadata_description(
                        MetadataPath::new("delegation-b").unwrap(),
                        MetadataDescription::from_slice(&[0u8], 1, &[HashAlgorithm::Sha256])
                            .unwrap(),
                    )
            })
            .unwrap()
            .commit()
            .await
            .unwrap();

        let mut tuf = Database::<Pouf1>::from_trusted_metadata(&metadata).unwrap();

        //// build delegation B ////

        let delegations = Delegations::builder()
            .key(delegation_b_key.public().clone())
            .role(
                Delegation::builder(MetadataPath::new("delegation-b").unwrap())
                    .key(delegation_b_key.public())
                    .delegate_path(TargetPath::new("foo").unwrap())
                    .build()
                    .unwrap(),
            )
            .build()
            .unwrap();

        let delegation = TargetsMetadataBuilder::new()
            .delegations(delegations)
            .signed::<Pouf1>(&delegation_a_key)
            .unwrap();
        let raw_delegation = delegation.to_raw().unwrap();

        tuf.update_delegated_targets(
            &now,
            &MetadataPath::targets(),
            &MetadataPath::new("delegation-a").unwrap(),
            &raw_delegation,
        )
        .unwrap();

        //// build delegation B ////

        let target_file: &[u8] = b"bar";

        let delegation = TargetsMetadataBuilder::new()
            .insert_target_from_slice(
                TargetPath::new("foo").unwrap(),
                target_file,
                &[HashAlgorithm::Sha256],
            )
            .unwrap()
            .signed::<Pouf1>(&delegation_b_key)
            .unwrap();
        let raw_delegation = delegation.to_raw().unwrap();

        tuf.update_delegated_targets(
            &now,
            &MetadataPath::new("delegation-a").unwrap(),
            &MetadataPath::new("delegation-b").unwrap(),
            &raw_delegation,
        )
        .unwrap();

        assert!(tuf
            .target_description(&TargetPath::new("foo").unwrap())
            .is_ok());
    })
}

#[test]
fn rejects_bad_delegation_signatures() {
    block_on(async {
        let now = Utc::now();

        let root_key = Ed25519PrivateKey::from_pkcs8(ED25519_1_PK8).unwrap();
        let snapshot_key = Ed25519PrivateKey::from_pkcs8(ED25519_2_PK8).unwrap();
        let targets_key = Ed25519PrivateKey::from_pkcs8(ED25519_3_PK8).unwrap();
        let timestamp_key = Ed25519PrivateKey::from_pkcs8(ED25519_4_PK8).unwrap();
        let delegation_key = Ed25519PrivateKey::from_pkcs8(ED25519_5_PK8).unwrap();
        let bad_delegation_key = Ed25519PrivateKey::from_pkcs8(ED25519_6_PK8).unwrap();

        let mut repo = EphemeralRepository::new();
        let metadata = RepoBuilder::create(&mut repo)
            .trusted_root_keys(&[&root_key])
            .trusted_snapshot_keys(&[&snapshot_key])
            .trusted_targets_keys(&[&targets_key])
            .trusted_timestamp_keys(&[&timestamp_key])
            .stage_root()
            .unwrap()
            .add_delegation_key(delegation_key.public().clone())
            .add_delegation_role(
                Delegation::builder(MetadataPath::new("delegation").unwrap())
                    .key(delegation_key.public())
                    .delegate_path(TargetPath::new("foo").unwrap())
                    .build()
                    .unwrap(),
            )
            .stage_targets()
            .unwrap()
            .stage_snapshot_with_builder(|builder| {
                builder.insert_metadata_description(
                    MetadataPath::new("delegation").unwrap(),
                    MetadataDescription::from_slice(&[0u8], 1, &[HashAlgorithm::Sha256]).unwrap(),
                )
            })
            .unwrap()
            .commit()
            .await
            .unwrap();

        let mut tuf = Database::<Pouf1>::from_trusted_metadata(&metadata).unwrap();

        //// build the delegation ////
        let target_file: &[u8] = b"bar";
        let delegation = TargetsMetadataBuilder::new()
            .insert_target_from_slice(
                TargetPath::new("foo").unwrap(),
                target_file,
                &[HashAlgorithm::Sha256],
            )
            .unwrap()
            .signed::<Pouf1>(&bad_delegation_key)
            .unwrap();
        let raw_delegation = delegation.to_raw().unwrap();

        assert_matches!(
            tuf.update_delegated_targets(
                &now,
                &MetadataPath::targets(),
                &MetadataPath::new("delegation").unwrap(),
                &raw_delegation
            ),
            Err(Error::MetadataMissingSignatures {
                role,
                number_of_valid_signatures: 0,
                threshold: 1,
            })
            if role == MetadataPath::new("delegation").unwrap()
        );

        let target_path = TargetPath::new("foo").unwrap();
        assert_matches!(
            tuf.target_description(&target_path),
            Err(Error::TargetNotFound(p)) if p == target_path
        );
    })
}

#[test]
fn diamond_delegation() {
    block_on(async {
        let now = Utc::now();

        let etc_key = Ed25519PrivateKey::from_pkcs8(ED25519_1_PK8).unwrap();
        let targets_key = Ed25519PrivateKey::from_pkcs8(ED25519_2_PK8).unwrap();
        let delegation_a_key = Ed25519PrivateKey::from_pkcs8(ED25519_3_PK8).unwrap();
        let delegation_b_key = Ed25519PrivateKey::from_pkcs8(ED25519_4_PK8).unwrap();
        let delegation_c_key = Ed25519PrivateKey::from_pkcs8(ED25519_5_PK8).unwrap();

        // Given delegations a, b, and c, targets delegates "foo" to delegation-a and "bar" to
        // delegation-b.
        //
        //             targets
        //              /  \
        //   delegation-a  delegation-b
        //              \  /
        //          delegation-c
        //
        // if delegation-a delegates "foo" to delegation-c, and
        //    delegation-b delegates "bar" to delegation-c, but
        //    delegation-b's signature is invalid, then delegation-c
        // can contain target "bar" which is unaccessible and target "foo" which is.
        //
        // Verify tuf::Database handles this situation correctly.

        //// build delegation A ////

        let delegations_a = Delegations::builder()
            .key(delegation_c_key.public().clone())
            .role(
                Delegation::builder(MetadataPath::new("delegation-c").unwrap())
                    .key(delegation_c_key.public())
                    .delegate_path(TargetPath::new("foo").unwrap())
                    .build()
                    .unwrap(),
            )
            .build()
            .unwrap();

        let delegation_a = TargetsMetadataBuilder::new()
            .delegations(delegations_a)
            .signed::<Pouf1>(&delegation_a_key)
            .unwrap();
        let raw_delegation_a = delegation_a.to_raw().unwrap();

        //// build delegation B ////

        let delegations_b = Delegations::builder()
            .key(delegation_c_key.public().clone())
            .role(
                Delegation::builder(MetadataPath::new("delegation-c").unwrap())
                    // oops, wrong key.
                    .key(delegation_b_key.public())
                    .delegate_path(TargetPath::new("foo").unwrap())
                    .build()
                    .unwrap(),
            )
            .build()
            .unwrap();

        let delegation_b = TargetsMetadataBuilder::new()
            .delegations(delegations_b)
            .signed::<Pouf1>(&delegation_b_key)
            .unwrap();
        let raw_delegation_b = delegation_b.to_raw().unwrap();

        //// build delegation C ////

        let foo_target_file: &[u8] = b"foo contents";
        let bar_target_file: &[u8] = b"bar contents";

        let delegation_c = TargetsMetadataBuilder::new()
            .insert_target_from_slice(
                TargetPath::new("foo").unwrap(),
                foo_target_file,
                &[HashAlgorithm::Sha256],
            )
            .unwrap()
            .insert_target_from_slice(
                TargetPath::new("bar").unwrap(),
                bar_target_file,
                &[HashAlgorithm::Sha256],
            )
            .unwrap()
            .signed::<Pouf1>(&delegation_c_key)
            .unwrap();
        let raw_delegation_c = delegation_c.to_raw().unwrap();

        //// construct the database ////

        let mut repo = EphemeralRepository::new();
        let metadata = RepoBuilder::create(&mut repo)
            .trusted_root_keys(&[&etc_key])
            .trusted_snapshot_keys(&[&etc_key])
            .trusted_targets_keys(&[&targets_key])
            .trusted_timestamp_keys(&[&etc_key])
            .stage_root()
            .unwrap()
            .add_delegation_key(delegation_a_key.public().clone())
            .add_delegation_key(delegation_b_key.public().clone())
            .add_delegation_role(
                Delegation::builder(MetadataPath::new("delegation-a").unwrap())
                    .key(delegation_a_key.public())
                    .delegate_path(TargetPath::new("foo").unwrap())
                    .build()
                    .unwrap(),
            )
            .add_delegation_role(
                Delegation::builder(MetadataPath::new("delegation-b").unwrap())
                    .key(delegation_b_key.public())
                    .delegate_path(TargetPath::new("bar").unwrap())
                    .build()
                    .unwrap(),
            )
            .stage_targets()
            .unwrap()
            .stage_snapshot_with_builder(|builder| {
                builder
                    .insert_metadata_description(
                        MetadataPath::new("delegation-a").unwrap(),
                        MetadataDescription::from_slice(
                            raw_delegation_a.as_bytes(),
                            1,
                            &[HashAlgorithm::Sha256],
                        )
                        .unwrap(),
                    )
                    .insert_metadata_description(
                        MetadataPath::new("delegation-b").unwrap(),
                        MetadataDescription::from_slice(
                            raw_delegation_b.as_bytes(),
                            1,
                            &[HashAlgorithm::Sha256],
                        )
                        .unwrap(),
                    )
                    .insert_metadata_description(
                        MetadataPath::new("delegation-c").unwrap(),
                        MetadataDescription::from_slice(
                            raw_delegation_c.as_bytes(),
                            1,
                            &[HashAlgorithm::Sha256],
                        )
                        .unwrap(),
                    )
            })
            .unwrap()
            .commit()
            .await
            .unwrap();

        let mut tuf = Database::<Pouf1>::from_trusted_metadata(&metadata).unwrap();

        //// Verify we can trust delegation-a and delegation-b..

        tuf.update_delegated_targets(
            &now,
            &MetadataPath::targets(),
            &MetadataPath::new("delegation-a").unwrap(),
            &raw_delegation_a,
        )
        .unwrap();

        tuf.update_delegated_targets(
            &now,
            &MetadataPath::targets(),
            &MetadataPath::new("delegation-b").unwrap(),
            &raw_delegation_b,
        )
        .unwrap();

        //// Verify delegation-c is valid, but only when updated through delegation-a.

        assert_matches!(
            tuf.update_delegated_targets(
                &now,
                &MetadataPath::new("delegation-b").unwrap(),
                &MetadataPath::new("delegation-c").unwrap(),
                &raw_delegation_c
            ),
            Err(Error::MetadataMissingSignatures {
                role,
                number_of_valid_signatures: 0,
                threshold: 1,
            })
            if role == MetadataPath::new("delegation-c").unwrap()
        );

        tuf.update_delegated_targets(
            &now,
            &MetadataPath::new("delegation-a").unwrap(),
            &MetadataPath::new("delegation-c").unwrap(),
            &raw_delegation_c,
        )
        .unwrap();

        assert!(tuf
            .target_description(&TargetPath::new("foo").unwrap())
            .is_ok());

        let target_path = TargetPath::new("bar").unwrap();
        assert_matches!(
            tuf.target_description(&target_path),
            Err(Error::TargetNotFound(p)) if p == target_path
        );
    })
}
