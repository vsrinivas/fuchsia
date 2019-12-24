// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::models::ActionDisplayInfo,
    anyhow::{format_err, Context as _, Error},
    fidl::endpoints::Proxy,
    fidl_fuchsia_io::DirectoryProxy,
    fidl_fuchsia_sys as fsys, fuchsia_async as fasync,
    fuchsia_component::client::connect_to_service,
    fuchsia_url::pkg_url::PkgUrl,
    io_util,
    serde::de::{Deserialize, Deserializer},
    serde_derive::Deserialize,
    std::collections::HashMap,
    std::path::PathBuf,
};

/// An `Action` describes a particular action to handle (e.g. com.fuchsia.navigate).
pub type Action = String;

/// An `ComponentUrl` is a Component URL to a module.
pub type ComponentUrl = String;

/// A `ParameterName` describes the name of an Action's parameter.
pub type ParameterName = String;

/// A `ParameterType` describes the type of an Action's parameter.
pub type ParameterType = String;

/// A `ModuleActionIndex` is a map from every supported `Action` to the set of modules which can
/// handle it.
#[allow(dead_code)]
pub type ModuleActionIndex = HashMap<Action, Vec<ModuleFacet>>;

/// A module facet contains the intent filters supported by a particular module.
#[derive(Clone, Debug, Deserialize)]
pub struct ModuleFacet {
    // The Component URL of module.
    pub component_url: Option<ComponentUrl>,

    // The intent filters for all the actions supported by the module.
    #[serde(default)]
    pub intent_filters: Vec<IntentFilter>,
}

// An intent filter describes an action, its parameters, and the parameter types.
#[derive(Clone, Debug, Deserialize, PartialEq)]
pub struct IntentFilter {
    // The action this intent filter describes.
    pub action: Action,

    // The parameters associated with `action`)]
    #[serde(default, deserialize_with = "deserialize_intent_parameters")]
    pub parameters: HashMap<ParameterName, ParameterType>,

    pub action_display: Option<ActionDisplayInfo>,
}

// A wrapper struct which is used for deserializing a `ModuleFacet` from a component manifest.
#[derive(Clone, Debug, Deserialize)]
struct ComponentManifest {
    // The facets which are part of the component manifest.
    facets: Option<Facets>,
}

// A wrapper struct which is used for deserializing a `ModuleFacet` from a component manifest.
#[derive(Clone, Debug, Deserialize)]
struct Facets {
    // The module facet is stored in the facets under the `fuchsia.module` key.
    #[serde(rename(deserialize = "fuchsia.modular"))]
    module: Option<ModuleFacet>,
}

/// Deserializes a vector of intent parameters into a `HashMap`.
///
/// Intent parameters are serialized as a vector of objects: [
///   {
///     name: ...
///     type: ...
///   }
/// ]
///
/// This helper converts the serialized vector into a `HashMap` of the form:
///
/// {
///   name => type
/// }
fn deserialize_intent_parameters<'de, D>(
    deserializer: D,
) -> Result<HashMap<ParameterName, ParameterType>, D::Error>
where
    D: Deserializer<'de>,
{
    #[derive(Deserialize, Hash, Eq, PartialEq)]
    struct IntentParameterEntry {
        name: ParameterName,
        r#type: ParameterType,
    }

    let entries: Vec<IntentParameterEntry> =
        Vec::<IntentParameterEntry>::deserialize(deserializer)?;

    let result = entries.into_iter().map(|entry| (entry.name, entry.r#type)).collect();
    Ok(result)
}

/// Creates a `ModuleActionIndex` from the provided `ModuleFacet`s.
///
/// Parameters:
/// - `facets`: The facets which will be used to create the index.
///
/// Returns:
/// An index for all the actions supported by `facets`.
#[allow(dead_code)]
pub fn index_facets(facets: &[ModuleFacet]) -> ModuleActionIndex {
    let mut index = ModuleActionIndex::new();
    for facet in facets {
        for intent_filter in &facet.intent_filters {
            index.entry(intent_filter.action.clone()).or_insert(Vec::new()).push(facet.clone());
        }
    }

    index
}

/// Loads the module facet from the component manifest for the specified component.
///
/// Parameters:
/// - `component_url`: The url for the component to load.
///
/// Returns:
/// A `ModuleFacet` parsed from the component manifest, or an `Error` on failure.
#[allow(dead_code)]
pub async fn load_module_facet(component_url: &str) -> Result<ModuleFacet, Error> {
    let loader =
        connect_to_service::<fsys::LoaderMarker>().context("Error connecting to loader.")?;

    let loader_result = loader
        .load_url(&component_url)
        .await
        .context(format!("Could not load package: {:?}", component_url))?;

    let package: fsys::Package = *loader_result.ok_or_else(|| {
        format_err!(format!("Loader result did not contain package: {:?}", component_url))
    })?;

    let cmx_file_contents: String = read_cmx_file(package).await?;
    let component_manifest: ComponentManifest = serde_json::from_str(&cmx_file_contents)?;

    let facets = component_manifest.facets.unwrap_or_else(|| Facets { module: None });
    let mut module = facets
        .module
        .unwrap_or_else(|| ModuleFacet { component_url: None, intent_filters: vec![] });
    module.component_url = Some(component_url.to_string());

    Ok(module)
}

/// Reads the cmx file contents from the provided `package`.
///
/// Parameters:
/// - `package`: The package to read the cmx file from.
///
/// Returns:
/// A `String` containing the contents of the cmx file, or an `Error` if reading the contents
/// failed.
#[allow(dead_code)]
async fn read_cmx_file(package: fsys::Package) -> Result<String, Error> {
    let package_uri = PkgUrl::parse(&package.resolved_url)
        .context("Could not parse the uri from the resolved url.")?;

    let name = package_uri.name().unwrap_or_else(|| "");

    let default_cmx_path;
    let cmx_path = if package_uri.resource().is_some() {
        package_uri.resource().unwrap()
    } else {
        default_cmx_path = format!("meta/{}.cmx", name);
        &default_cmx_path
    };

    let package_directory =
        package.directory.ok_or_else(|| format_err!("Package does not contain directory."))?;
    let channel = fasync::Channel::from_channel(package_directory)
        .context("Could not create channel from package directory")?;

    let directory_proxy = DirectoryProxy::from_channel(channel);

    let cmx_file = io_util::open_file(
        &directory_proxy,
        &PathBuf::from(cmx_path),
        io_util::OPEN_RIGHT_READABLE,
    )
    .context("Could not open cmx file for package.")?;

    io_util::read_file(&cmx_file).await
}
