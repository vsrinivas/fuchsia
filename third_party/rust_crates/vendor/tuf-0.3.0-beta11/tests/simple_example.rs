use futures_executor::block_on;
use futures_util::io::Cursor;
use tuf::client::{Client, Config};
use tuf::crypto::{Ed25519PrivateKey, PrivateKey, PublicKey};
use tuf::metadata::{MetadataVersion, TargetPath};
use tuf::pouf::Pouf1;
use tuf::repo_builder::RepoBuilder;
use tuf::repository::EphemeralRepository;
use tuf::Result;

// Ironically, this is far from simple, but it's as simple as it can be made.

const ED25519_1_PK8: &[u8] = include_bytes!("./ed25519/ed25519-1.pk8.der");
const ED25519_2_PK8: &[u8] = include_bytes!("./ed25519/ed25519-2.pk8.der");
const ED25519_3_PK8: &[u8] = include_bytes!("./ed25519/ed25519-3.pk8.der");
const ED25519_4_PK8: &[u8] = include_bytes!("./ed25519/ed25519-4.pk8.der");

#[test]
fn consistent_snapshot_false() {
    block_on(async {
        let config = Config::default();

        run_tests(config, false).await
    })
}

#[test]
fn consistent_snapshot_true() {
    block_on(async {
        let config = Config::default();

        run_tests(config, true).await
    })
}

async fn run_tests(config: Config, consistent_snapshots: bool) {
    let mut remote = EphemeralRepository::new();
    let root_public_keys = init_server(&mut remote, consistent_snapshots)
        .await
        .unwrap();

    init_client(&root_public_keys, remote, config)
        .await
        .unwrap();
}

async fn init_client(
    root_public_keys: &[PublicKey],
    remote: EphemeralRepository<Pouf1>,
    config: Config,
) -> Result<()> {
    let local = EphemeralRepository::new();
    let mut client = Client::with_trusted_root_keys(
        config,
        MetadataVersion::Number(1),
        1,
        root_public_keys,
        local,
        remote,
    )
    .await?;
    let _ = client.update().await?;
    let target_path = TargetPath::new("foo-bar")?;
    client.fetch_target_to_local(&target_path).await
}

async fn init_server(
    remote: &mut EphemeralRepository<Pouf1>,
    consistent_snapshot: bool,
) -> Result<Vec<PublicKey>> {
    // in real life, you wouldn't want these keys on the same machine ever
    let root_key = Ed25519PrivateKey::from_pkcs8(ED25519_1_PK8)?;
    let snapshot_key = Ed25519PrivateKey::from_pkcs8(ED25519_2_PK8)?;
    let targets_key = Ed25519PrivateKey::from_pkcs8(ED25519_3_PK8)?;
    let timestamp_key = Ed25519PrivateKey::from_pkcs8(ED25519_4_PK8)?;

    let target_path = TargetPath::new("foo-bar")?;
    let target_file: &[u8] = b"things fade, alternatives exclude";

    let _metadata = RepoBuilder::create(&mut *remote)
        .trusted_root_keys(&[&root_key])
        .trusted_snapshot_keys(&[&snapshot_key])
        .trusted_targets_keys(&[&targets_key])
        .trusted_timestamp_keys(&[&timestamp_key])
        .stage_root_with_builder(|builder| builder.consistent_snapshot(consistent_snapshot))
        .unwrap()
        .add_target(target_path.clone(), Cursor::new(target_file))
        .await
        .unwrap()
        .commit()
        .await
        .unwrap();

    Ok(vec![root_key.public().clone()])
}
