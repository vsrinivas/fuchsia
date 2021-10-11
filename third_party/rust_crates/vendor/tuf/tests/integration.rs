use maplit::hashmap;
use matches::assert_matches;
use std::iter::once;
use tuf::crypto::{Ed25519PrivateKey, HashAlgorithm, PrivateKey};
use tuf::interchange::Json;
use tuf::metadata::{
    Delegation, Delegations, MetadataDescription, MetadataPath, Role, RootMetadataBuilder,
    SnapshotMetadataBuilder, TargetsMetadataBuilder, TimestampMetadataBuilder, VirtualTargetPath,
};
use tuf::Error;
use tuf::Tuf;

const ED25519_1_PK8: &'static [u8] = include_bytes!("./ed25519/ed25519-1.pk8.der");
const ED25519_2_PK8: &'static [u8] = include_bytes!("./ed25519/ed25519-2.pk8.der");
const ED25519_3_PK8: &'static [u8] = include_bytes!("./ed25519/ed25519-3.pk8.der");
const ED25519_4_PK8: &'static [u8] = include_bytes!("./ed25519/ed25519-4.pk8.der");
const ED25519_5_PK8: &'static [u8] = include_bytes!("./ed25519/ed25519-5.pk8.der");
const ED25519_6_PK8: &'static [u8] = include_bytes!("./ed25519/ed25519-6.pk8.der");

#[test]
fn simple_delegation() {
    let root_key = Ed25519PrivateKey::from_pkcs8(ED25519_1_PK8).unwrap();
    let snapshot_key = Ed25519PrivateKey::from_pkcs8(ED25519_2_PK8).unwrap();
    let targets_key = Ed25519PrivateKey::from_pkcs8(ED25519_3_PK8).unwrap();
    let timestamp_key = Ed25519PrivateKey::from_pkcs8(ED25519_4_PK8).unwrap();
    let delegation_key = Ed25519PrivateKey::from_pkcs8(ED25519_5_PK8).unwrap();

    //// build the root ////

    let root = RootMetadataBuilder::new()
        .root_key(root_key.public().clone())
        .snapshot_key(snapshot_key.public().clone())
        .targets_key(targets_key.public().clone())
        .timestamp_key(timestamp_key.public().clone())
        .signed::<Json>(&root_key)
        .unwrap();
    let raw_root = root.to_raw().unwrap();

    let mut tuf =
        Tuf::<Json>::from_root_with_trusted_keys(&raw_root, 1, once(root_key.public())).unwrap();

    //// build the snapshot and timestamp ////

    let snapshot = SnapshotMetadataBuilder::new()
        .insert_metadata_description(
            MetadataPath::new("targets").unwrap(),
            MetadataDescription::from_reader(&*vec![0u8], 1, &[HashAlgorithm::Sha256]).unwrap(),
        )
        .insert_metadata_description(
            MetadataPath::new("delegation").unwrap(),
            MetadataDescription::from_reader(&*vec![0u8], 1, &[HashAlgorithm::Sha256]).unwrap(),
        )
        .signed::<Json>(&snapshot_key)
        .unwrap();
    let raw_snapshot = snapshot.to_raw().unwrap();

    let timestamp = TimestampMetadataBuilder::from_snapshot(&snapshot, &[HashAlgorithm::Sha256])
        .unwrap()
        .signed::<Json>(&timestamp_key)
        .unwrap();
    let raw_timestamp = timestamp.to_raw().unwrap();

    tuf.update_timestamp(&raw_timestamp).unwrap();
    tuf.update_snapshot(&raw_snapshot).unwrap();

    //// build the targets ////
    let delegations = Delegations::new(
        hashmap! { delegation_key.public().key_id().clone() => delegation_key.public().clone() },
        vec![Delegation::new(
            MetadataPath::new("delegation").unwrap(),
            false,
            1,
            vec![delegation_key.public().key_id().clone()]
                .iter()
                .cloned()
                .collect(),
            vec![VirtualTargetPath::new("foo".into()).unwrap()]
                .iter()
                .cloned()
                .collect(),
        )
        .unwrap()],
    )
    .unwrap();
    let targets = TargetsMetadataBuilder::new()
        .delegations(delegations)
        .signed::<Json>(&targets_key)
        .unwrap();
    let raw_targets = targets.to_raw().unwrap();

    tuf.update_targets(&raw_targets).unwrap();

    //// build the delegation ////
    let target_file: &[u8] = b"bar";
    let delegation = TargetsMetadataBuilder::new()
        .insert_target_from_reader(
            VirtualTargetPath::new("foo".into()).unwrap(),
            target_file,
            &[HashAlgorithm::Sha256],
        )
        .unwrap()
        .signed::<Json>(&delegation_key)
        .unwrap();
    let raw_delegation = delegation.to_raw().unwrap();

    tuf.update_delegation(
        &MetadataPath::from_role(&Role::Targets),
        &MetadataPath::new("delegation").unwrap(),
        &raw_delegation,
    )
    .unwrap();

    assert!(tuf
        .target_description(&VirtualTargetPath::new("foo".into()).unwrap())
        .is_ok());
}

#[test]
fn nested_delegation() {
    let root_key = Ed25519PrivateKey::from_pkcs8(ED25519_1_PK8).unwrap();
    let snapshot_key = Ed25519PrivateKey::from_pkcs8(ED25519_2_PK8).unwrap();
    let targets_key = Ed25519PrivateKey::from_pkcs8(ED25519_3_PK8).unwrap();
    let timestamp_key = Ed25519PrivateKey::from_pkcs8(ED25519_4_PK8).unwrap();
    let delegation_a_key = Ed25519PrivateKey::from_pkcs8(ED25519_5_PK8).unwrap();
    let delegation_b_key = Ed25519PrivateKey::from_pkcs8(ED25519_6_PK8).unwrap();

    //// build the root ////

    let root = RootMetadataBuilder::new()
        .root_key(root_key.public().clone())
        .snapshot_key(snapshot_key.public().clone())
        .targets_key(targets_key.public().clone())
        .timestamp_key(timestamp_key.public().clone())
        .signed::<Json>(&root_key)
        .unwrap();
    let raw_root = root.to_raw().unwrap();

    let mut tuf =
        Tuf::<Json>::from_root_with_trusted_keys(&raw_root, 1, once(root_key.public())).unwrap();

    //// build the snapshot and timestamp ////

    let snapshot = SnapshotMetadataBuilder::new()
        .insert_metadata_description(
            MetadataPath::new("targets").unwrap(),
            MetadataDescription::from_reader(&*vec![0u8], 1, &[HashAlgorithm::Sha256]).unwrap(),
        )
        .insert_metadata_description(
            MetadataPath::new("delegation-a").unwrap(),
            MetadataDescription::from_reader(&*vec![0u8], 1, &[HashAlgorithm::Sha256]).unwrap(),
        )
        .insert_metadata_description(
            MetadataPath::new("delegation-b").unwrap(),
            MetadataDescription::from_reader(&*vec![0u8], 1, &[HashAlgorithm::Sha256]).unwrap(),
        )
        .signed::<Json>(&snapshot_key)
        .unwrap();
    let raw_snapshot = snapshot.to_raw().unwrap();

    let timestamp = TimestampMetadataBuilder::from_snapshot(&snapshot, &[HashAlgorithm::Sha256])
        .unwrap()
        .signed::<Json>(&timestamp_key)
        .unwrap();
    let raw_timestamp = timestamp.to_raw().unwrap();

    tuf.update_timestamp(&raw_timestamp).unwrap();
    tuf.update_snapshot(&raw_snapshot).unwrap();

    //// build the targets ////

    let delegations = Delegations::new(
        hashmap! {
            delegation_a_key.public().key_id().clone() => delegation_a_key.public().clone(),
        },
        vec![Delegation::new(
            MetadataPath::new("delegation-a").unwrap(),
            false,
            1,
            vec![delegation_a_key.public().key_id().clone()]
                .iter()
                .cloned()
                .collect(),
            vec![VirtualTargetPath::new("foo".into()).unwrap()]
                .iter()
                .cloned()
                .collect(),
        )
        .unwrap()],
    )
    .unwrap();
    let targets = TargetsMetadataBuilder::new()
        .delegations(delegations)
        .signed::<Json>(&targets_key)
        .unwrap();
    let raw_targets = targets.to_raw().unwrap();

    tuf.update_targets(&raw_targets).unwrap();

    //// build delegation A ////

    let delegations = Delegations::new(
        hashmap! { delegation_b_key.public().key_id().clone() => delegation_b_key.public().clone() },
        vec![Delegation::new(
            MetadataPath::new("delegation-b").unwrap(),
            false,
            1,
            vec![delegation_b_key.public().key_id().clone()].iter().cloned().collect(),
            vec![VirtualTargetPath::new("foo".into()).unwrap()].iter().cloned().collect(),
        )
        .unwrap()],
    )
    .unwrap();

    let delegation = TargetsMetadataBuilder::new()
        .delegations(delegations)
        .signed::<Json>(&delegation_a_key)
        .unwrap();
    let raw_delegation = delegation.to_raw().unwrap();

    tuf.update_delegation(
        &MetadataPath::from_role(&Role::Targets),
        &MetadataPath::new("delegation-a").unwrap(),
        &raw_delegation,
    )
    .unwrap();

    //// build delegation B ////

    let target_file: &[u8] = b"bar";

    let delegation = TargetsMetadataBuilder::new()
        .insert_target_from_reader(
            VirtualTargetPath::new("foo".into()).unwrap(),
            target_file,
            &[HashAlgorithm::Sha256],
        )
        .unwrap()
        .signed::<Json>(&delegation_b_key)
        .unwrap();
    let raw_delegation = delegation.to_raw().unwrap();

    tuf.update_delegation(
        &MetadataPath::new("delegation-a").unwrap(),
        &MetadataPath::new("delegation-b").unwrap(),
        &raw_delegation,
    )
    .unwrap();

    assert!(tuf
        .target_description(&VirtualTargetPath::new("foo".into()).unwrap())
        .is_ok());
}

#[test]
fn rejects_bad_delegation_signatures() {
    let root_key = Ed25519PrivateKey::from_pkcs8(ED25519_1_PK8).unwrap();
    let snapshot_key = Ed25519PrivateKey::from_pkcs8(ED25519_2_PK8).unwrap();
    let targets_key = Ed25519PrivateKey::from_pkcs8(ED25519_3_PK8).unwrap();
    let timestamp_key = Ed25519PrivateKey::from_pkcs8(ED25519_4_PK8).unwrap();
    let delegation_key = Ed25519PrivateKey::from_pkcs8(ED25519_5_PK8).unwrap();
    let bad_delegation_key = Ed25519PrivateKey::from_pkcs8(ED25519_6_PK8).unwrap();

    //// build the root ////

    let root = RootMetadataBuilder::new()
        .root_key(root_key.public().clone())
        .snapshot_key(snapshot_key.public().clone())
        .targets_key(targets_key.public().clone())
        .timestamp_key(timestamp_key.public().clone())
        .signed::<Json>(&root_key)
        .unwrap();
    let raw_root = root.to_raw().unwrap();

    let mut tuf =
        Tuf::<Json>::from_root_with_trusted_keys(&raw_root, 1, once(root_key.public())).unwrap();

    //// build the snapshot and timestamp ////

    let snapshot = SnapshotMetadataBuilder::new()
        .insert_metadata_description(
            MetadataPath::new("targets").unwrap(),
            MetadataDescription::from_reader(&*vec![0u8], 1, &[HashAlgorithm::Sha256]).unwrap(),
        )
        .insert_metadata_description(
            MetadataPath::new("delegation").unwrap(),
            MetadataDescription::from_reader(&*vec![0u8], 1, &[HashAlgorithm::Sha256]).unwrap(),
        )
        .signed::<Json>(&snapshot_key)
        .unwrap();
    let raw_snapshot = snapshot.to_raw().unwrap();

    let timestamp = TimestampMetadataBuilder::from_snapshot(&snapshot, &[HashAlgorithm::Sha256])
        .unwrap()
        .signed::<Json>(&timestamp_key)
        .unwrap();
    let raw_timestamp = timestamp.to_raw().unwrap();

    tuf.update_timestamp(&raw_timestamp).unwrap();
    tuf.update_snapshot(&raw_snapshot).unwrap();

    //// build the targets ////
    let delegations = Delegations::new(
        hashmap! { delegation_key.public().key_id().clone() => delegation_key.public().clone() },
        vec![Delegation::new(
            MetadataPath::new("delegation").unwrap(),
            false,
            1,
            vec![delegation_key.public().key_id().clone()]
                .iter()
                .cloned()
                .collect(),
            vec![VirtualTargetPath::new("foo".into()).unwrap()]
                .iter()
                .cloned()
                .collect(),
        )
        .unwrap()],
    )
    .unwrap();
    let targets = TargetsMetadataBuilder::new()
        .delegations(delegations)
        .signed::<Json>(&targets_key)
        .unwrap();
    let raw_targets = targets.to_raw().unwrap();

    tuf.update_targets(&raw_targets).unwrap();

    //// build the delegation ////
    let target_file: &[u8] = b"bar";
    let delegation = TargetsMetadataBuilder::new()
        .insert_target_from_reader(
            VirtualTargetPath::new("foo".into()).unwrap(),
            target_file,
            &[HashAlgorithm::Sha256],
        )
        .unwrap()
        .signed::<Json>(&bad_delegation_key)
        .unwrap();
    let raw_delegation = delegation.to_raw().unwrap();

    assert_matches!(
        tuf.update_delegation(
            &MetadataPath::from_role(&Role::Targets),
            &MetadataPath::new("delegation").unwrap(),
            &raw_delegation
        ),
        Err(Error::VerificationFailure(_))
    );

    assert_matches!(
        tuf.target_description(&VirtualTargetPath::new("foo".into()).unwrap()),
        Err(Error::TargetUnavailable)
    );
}

#[test]
fn diamond_delegation() {
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
    // Verify tuf::Tuf handles this situation correctly.

    //// build the root ////

    let root = RootMetadataBuilder::new()
        .root_key(etc_key.public().clone())
        .snapshot_key(etc_key.public().clone())
        .targets_key(targets_key.public().clone())
        .timestamp_key(etc_key.public().clone())
        .signed::<Json>(&etc_key)
        .unwrap();
    let raw_root = root.to_raw().unwrap();

    let mut tuf =
        Tuf::<Json>::from_root_with_trusted_keys(&raw_root, 1, once(etc_key.public())).unwrap();

    //// build the snapshot and timestamp ////

    let snapshot = SnapshotMetadataBuilder::new()
        .insert_metadata_description(
            MetadataPath::new("targets").unwrap(),
            MetadataDescription::from_reader(&*vec![0u8], 1, &[HashAlgorithm::Sha256]).unwrap(),
        )
        .insert_metadata_description(
            MetadataPath::new("delegation-a").unwrap(),
            MetadataDescription::from_reader(&*vec![0u8], 1, &[HashAlgorithm::Sha256]).unwrap(),
        )
        .insert_metadata_description(
            MetadataPath::new("delegation-b").unwrap(),
            MetadataDescription::from_reader(&*vec![0u8], 1, &[HashAlgorithm::Sha256]).unwrap(),
        )
        .insert_metadata_description(
            MetadataPath::new("delegation-c").unwrap(),
            MetadataDescription::from_reader(&*vec![0u8], 1, &[HashAlgorithm::Sha256]).unwrap(),
        )
        .signed::<Json>(&etc_key)
        .unwrap();
    let raw_snapshot = snapshot.to_raw().unwrap();

    let timestamp = TimestampMetadataBuilder::from_snapshot(&snapshot, &[HashAlgorithm::Sha256])
        .unwrap()
        .signed::<Json>(&etc_key)
        .unwrap();
    let raw_timestamp = timestamp.to_raw().unwrap();

    tuf.update_timestamp(&raw_timestamp).unwrap();
    tuf.update_snapshot(&raw_snapshot).unwrap();

    //// build the targets ////

    let delegations = Delegations::new(
        hashmap! {
            delegation_a_key.public().key_id().clone() => delegation_a_key.public().clone(),
            delegation_b_key.public().key_id().clone() => delegation_b_key.public().clone(),
        },
        vec![
            Delegation::new(
                MetadataPath::new("delegation-a").unwrap(),
                false,
                1,
                vec![delegation_a_key.public().key_id().clone()]
                    .iter()
                    .cloned()
                    .collect(),
                vec![VirtualTargetPath::new("foo".into()).unwrap()]
                    .iter()
                    .cloned()
                    .collect(),
            )
            .unwrap(),
            Delegation::new(
                MetadataPath::new("delegation-b").unwrap(),
                false,
                1,
                vec![delegation_b_key.public().key_id().clone()]
                    .iter()
                    .cloned()
                    .collect(),
                vec![VirtualTargetPath::new("bar".into()).unwrap()]
                    .iter()
                    .cloned()
                    .collect(),
            )
            .unwrap(),
        ],
    )
    .unwrap();
    let targets = TargetsMetadataBuilder::new()
        .delegations(delegations)
        .signed::<Json>(&targets_key)
        .unwrap();
    let raw_targets = targets.to_raw().unwrap();

    tuf.update_targets(&raw_targets).unwrap();

    //// build delegation A ////

    let delegations = Delegations::new(
        hashmap! { delegation_c_key.public().key_id().clone() => delegation_c_key.public().clone() },
        vec![Delegation::new(
            MetadataPath::new("delegation-c").unwrap(),
            false,
            1,
            vec![delegation_c_key.public().key_id().clone()].iter().cloned().collect(),
            vec![VirtualTargetPath::new("foo".into()).unwrap()].iter().cloned().collect(),
        )
        .unwrap()],
    )
    .unwrap();

    let delegation = TargetsMetadataBuilder::new()
        .delegations(delegations)
        .signed::<Json>(&delegation_a_key)
        .unwrap();
    let raw_delegation = delegation.to_raw().unwrap();

    tuf.update_delegation(
        &MetadataPath::from_role(&Role::Targets),
        &MetadataPath::new("delegation-a").unwrap(),
        &raw_delegation,
    )
    .unwrap();

    //// build delegation B ////

    let delegations = Delegations::new(
        hashmap! { delegation_c_key.public().key_id().clone() => delegation_c_key.public().clone() },
        vec![Delegation::new(
            MetadataPath::new("delegation-c").unwrap(),
            false,
            1,
            // oops, wrong key.
            vec![delegation_b_key.public().key_id().clone()].iter().cloned().collect(),
            vec![VirtualTargetPath::new("bar".into()).unwrap()].iter().cloned().collect(),
        )
        .unwrap()],
    )
    .unwrap();

    let delegation = TargetsMetadataBuilder::new()
        .delegations(delegations)
        .signed::<Json>(&delegation_b_key)
        .unwrap();
    let raw_delegation = delegation.to_raw().unwrap();

    tuf.update_delegation(
        &MetadataPath::from_role(&Role::Targets),
        &MetadataPath::new("delegation-b").unwrap(),
        &raw_delegation,
    )
    .unwrap();

    //// build delegation C ////

    let foo_target_file: &[u8] = b"foo contents";
    let bar_target_file: &[u8] = b"bar contents";

    let delegation = TargetsMetadataBuilder::new()
        .insert_target_from_reader(
            VirtualTargetPath::new("foo".into()).unwrap(),
            foo_target_file,
            &[HashAlgorithm::Sha256],
        )
        .unwrap()
        .insert_target_from_reader(
            VirtualTargetPath::new("bar".into()).unwrap(),
            bar_target_file,
            &[HashAlgorithm::Sha256],
        )
        .unwrap()
        .signed::<Json>(&delegation_c_key)
        .unwrap();
    let raw_delegation = delegation.to_raw().unwrap();

    //// Verify delegation-c is valid, but only when updated through delegation-a.

    tuf.update_delegation(
        &MetadataPath::new("delegation-a").unwrap(),
        &MetadataPath::new("delegation-c").unwrap(),
        &raw_delegation,
    )
    .unwrap();

    assert_matches!(
        tuf.update_delegation(
            &MetadataPath::new("delegation-b").unwrap(),
            &MetadataPath::new("delegation-c").unwrap(),
            &raw_delegation
        ),
        Err(Error::VerificationFailure(_))
    );

    assert!(tuf
        .target_description(&VirtualTargetPath::new("foo".into()).unwrap())
        .is_ok());

    assert_matches!(
        tuf.target_description(&VirtualTargetPath::new("bar".into()).unwrap()),
        Err(Error::TargetUnavailable)
    );
}
