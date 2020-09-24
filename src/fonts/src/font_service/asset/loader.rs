use {
    super::collection::AssetCollectionError,
    anyhow::{format_err, Error},
    async_trait::async_trait,
    fidl::endpoints::create_proxy,
    fidl_fuchsia_io as io, fidl_fuchsia_mem as mem,
    fidl_fuchsia_pkg::{FontResolverMarker, UpdatePolicy},
    fuchsia_component::client::connect_to_service,
    fuchsia_trace as trace, fuchsia_zircon as zx,
    manifest::v2,
    std::{fs::File, path::Path},
};

/// A trait that covers the interactions of the font service with `fuchsia.pkg.FontResolver` and
/// font asset VMOs. Intended for easier testing.
#[async_trait]
pub trait AssetLoader {
    /// Resolves a font package to its `Directory`.
    async fn fetch_package_directory(
        &self,
        package_locator: &v2::PackageLocator,
    ) -> Result<io::DirectoryProxy, AssetCollectionError>;

    /// Gets a `VMO` handle to the [`Asset`] at a local `path`.
    fn load_vmo_from_path(&self, path: &Path) -> Result<mem::Buffer, AssetCollectionError>;

    /// Gets a VMO handle to the file with the given `file_name` within the given directory.
    /// The `package_locator` is used for detailed error messages.
    async fn load_vmo_from_directory_proxy(
        &self,
        directory_proxy: io::DirectoryProxy,
        package_locator: &v2::PackageLocator,
        file_name: &str,
    ) -> Result<mem::Buffer, AssetCollectionError>;
}

/// Real implementation of [`AssetLoader`].
pub struct AssetLoaderImpl {}

impl AssetLoaderImpl {
    /// Creates a new instance of `AssetLoaderImpl`.
    pub fn new() -> Self {
        AssetLoaderImpl {}
    }
}

/// This implementation is currently covered by integration tests only.
/// TODO(fxbug.dev/48649): Unit tests.
#[async_trait]
impl AssetLoader for AssetLoaderImpl {
    async fn fetch_package_directory(
        &self,
        package_locator: &v2::PackageLocator,
    ) -> Result<io::DirectoryProxy, AssetCollectionError> {
        let package_url = package_locator.url.to_string();
        trace::duration!(
            "fonts",
            "asset:fetcher:fetch_package_directory",
            "package_url" => &package_url[..]);

        // Get directory handle from FontResolver
        let font_resolver = connect_to_service::<FontResolverMarker>()
            .map_err(|e| AssetCollectionError::ServiceConnectionError(e.into()))?;
        let mut update_policy = UpdatePolicy { fetch_if_absent: true, allow_old_versions: false };
        let (dir_proxy, dir_request) = create_proxy::<io::DirectoryMarker>()
            .map_err(|e| AssetCollectionError::ServiceConnectionError(e.into()))?;

        let response =
            font_resolver.resolve(&package_url, &mut update_policy, dir_request).await.map_err(
                |e| AssetCollectionError::PackageResolverError(package_locator.clone(), e.into()),
            )?;
        let () = response.map_err(|i| {
            AssetCollectionError::PackageResolverError(
                package_locator.clone(),
                zx::Status::from_raw(i).into(),
            )
        })?;

        Ok(dir_proxy)
    }

    fn load_vmo_from_path(&self, path: &Path) -> Result<mem::Buffer, AssetCollectionError> {
        let path_string = path.to_str().unwrap_or_default();
        trace::duration!(
                "fonts",
                "asset:fetcher:load_vmo_from_path",
                "path" => path_string);
        let file = File::open(path)
            .map_err(|e| AssetCollectionError::LocalFileNotAccessible(path.to_owned(), e.into()))?;
        let vmo = fdio::get_vmo_copy_from_file(&file)
            .map_err(|e| AssetCollectionError::LocalFileNotAccessible(path.to_owned(), e.into()))?;
        let size = file
            .metadata()
            .map_err(|e| AssetCollectionError::LocalFileNotAccessible(path.to_owned(), e.into()))?
            .len();
        Ok(mem::Buffer { vmo, size })
    }

    async fn load_vmo_from_directory_proxy(
        &self,
        directory_proxy: io::DirectoryProxy,
        package_locator: &v2::PackageLocator,
        file_name: &str,
    ) -> Result<mem::Buffer, AssetCollectionError> {
        trace::duration!(
            "fonts",
            "asset:collection:load_buffer_from_directory_proxy",
            "file_name" => file_name);

        let packaged_file_error = |cause: Error| AssetCollectionError::PackagedFileError {
            file_name: file_name.to_string(),
            package_locator: package_locator.clone(),
            cause,
        };

        let file_proxy =
            io_util::open_file(&directory_proxy, Path::new(&file_name), io::OPEN_RIGHT_READABLE)
                .map_err(|e| packaged_file_error(e))?;

        let (status, buffer) = file_proxy
            .get_buffer(io::VMO_FLAG_READ)
            .await
            .map_err(|e| packaged_file_error(e.into()))?;

        zx::Status::ok(status).map_err(|e| packaged_file_error(e.into()))?;

        buffer.map(|b| *b).ok_or_else(|| {
            packaged_file_error(
                format_err!(
                    "Inexplicably failed to access buffer after opening the file successfully"
                )
                .into(),
            )
        })
    }
}
