// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file

use {
    super::util::*,
    crate::{FONTS_LARGE_CM, FONTS_MEDIUM_CM, FONTS_SMALL_CM},
    fidl_fuchsia_fonts_experimental as fonts_exp,
};

// Add new tests here so we don't overload component manager with requests (58150)
#[fasync::run_singlethreaded(test)]
async fn test_list_typefaces() {
    test_list_typefaces_empty_request_gets_all().await.unwrap();
    test_list_typefaces_no_results_after_last_page().await.unwrap();
    test_list_typefaces_paginates().await.unwrap();
    test_list_typefaces_no_results_found().await.unwrap();
    test_list_typefaces_by_name().await.unwrap();
    test_list_typefaces_by_alias().await.unwrap();
    test_list_typefaces_by_name_ignores_case().await.unwrap();
    test_list_typefaces_by_name_substring().await.unwrap();
    test_list_typefaces_by_slant_range().await.unwrap();
    test_list_typefaces_by_slant_range_is_inclusive().await.unwrap();
    test_list_typefaces_by_weight_range().await.unwrap();
    test_list_typefaces_by_weight_range_is_inclusive().await.unwrap();
    test_list_typefaces_by_width_range().await.unwrap();
    test_list_typefaces_by_width_range_is_inclusive().await.unwrap();
    test_list_typefaces_by_language().await.unwrap();
    test_list_typefaces_by_code_point().await.unwrap();
    test_list_typefaces_by_generic_family().await.unwrap();
}

async fn test_list_typefaces_empty_request_gets_all() -> Result<(), Error> {
    let font_provider = get_provider(FONTS_MEDIUM_CM).await?;
    let (client, iterator) = create_proxy::<fonts_exp::ListTypefacesIteratorMarker>()?;

    let request = empty_list_typefaces_request();

    font_provider.list_typefaces(request, iterator).await?.expect("ListTypefaces request failed");

    let response = client.get_next().await?;
    let results = response.results.unwrap();

    assert!(results.len() >= 12, "{:?}", results);
    Ok(())
}

async fn test_list_typefaces_no_results_after_last_page() -> Result<(), Error> {
    let font_provider = get_provider(FONTS_SMALL_CM).await?;
    let (client, iterator) = create_proxy::<fonts_exp::ListTypefacesIteratorMarker>()?;

    let request = empty_list_typefaces_request();

    font_provider
        .list_typefaces(request, iterator.into())
        .await?
        .expect("ListTypefaces request failed");

    let first = client.get_next().await?.results.unwrap();
    let second = client.get_next().await?.results.unwrap();

    assert!(!first.is_empty(), "{:?}", first);
    assert!(second.is_empty(), "{:?}", second);
    Ok(())
}

async fn test_list_typefaces_paginates() -> Result<(), Error> {
    // Load all fonts to ensure results must be paginated
    let font_provider = get_provider(FONTS_LARGE_CM).await?;
    let (client, iterator) = create_proxy::<fonts_exp::ListTypefacesIteratorMarker>()?;

    let request = empty_list_typefaces_request();

    font_provider
        .list_typefaces(request, iterator.into())
        .await?
        .expect("ListTypefaces request failed");

    let first = client.get_next().await?.results.unwrap();
    let second = client.get_next().await?.results.unwrap();

    assert!(!first.is_empty(), "{:?}", first);
    assert!(!second.is_empty(), "{:?}", second);

    // Results should be in manifest order
    assert!(
        first
            .iter()
            .any(|f| f.family
                == Some(fonts::FamilyName { name: "Material Design Icons".to_string() }))
    );
    assert!(second
        .iter()
        .any(|f| f.family.is_some() && f.family.as_ref().unwrap().name.starts_with("Noto")));

    // Pages should not share elements
    for result in first {
        assert!(!second.contains(&result));
    }
    Ok(())
}

async fn test_list_typefaces_no_results_found() -> Result<(), Error> {
    let font_provider = get_provider(FONTS_MEDIUM_CM).await?;
    let (client, iterator) = create_proxy::<fonts_exp::ListTypefacesIteratorMarker>()?;

    let request = name_query("404FontNotFound");

    font_provider
        .list_typefaces(request, iterator.into())
        .await?
        .expect("ListTypefaces request failed");

    let response = client.get_next().await?;
    let results = response.results.unwrap();

    assert!(results.is_empty(), "{:?}", results);
    Ok(())
}

async fn test_list_typefaces_by_name() -> Result<(), Error> {
    let font_provider = get_provider(FONTS_MEDIUM_CM).await?;
    let (client, iterator) = create_proxy::<fonts_exp::ListTypefacesIteratorMarker>()?;

    let request = name_query("Roboto");

    font_provider
        .list_typefaces(request, iterator.into())
        .await?
        .expect("ListTypefaces request failed");

    let response = client.get_next().await?;
    let results = response.results.unwrap();

    assert_eq!(results.len(), 3, "{:?}", results);
    for result in &results {
        assert_eq!(result.family.as_ref().unwrap().name, "Roboto");
    }
    Ok(())
}

async fn test_list_typefaces_by_alias() -> Result<(), Error> {
    let font_provider = get_provider(FONTS_MEDIUM_CM).await?;
    let (client, iterator) = create_proxy::<fonts_exp::ListTypefacesIteratorMarker>()?;

    let request = name_query("MaterialIcons");

    font_provider
        .list_typefaces(request, iterator.into())
        .await?
        .expect("ListTypefaces request failed");

    let response = client.get_next().await?;
    let results = response.results.unwrap();

    assert_eq!(results.len(), 1, "{:?}", results);
    assert_eq!(results[0].family.as_ref().unwrap().name, "Material Design Icons");
    Ok(())
}

async fn test_list_typefaces_by_name_ignores_case() -> Result<(), Error> {
    let font_provider = get_provider(FONTS_MEDIUM_CM).await?;
    let (client, iterator) = create_proxy::<fonts_exp::ListTypefacesIteratorMarker>()?;

    let request = name_query("roboto");

    font_provider
        .list_typefaces(request, iterator.into())
        .await?
        .expect("ListTypefaces request failed");

    let response = client.get_next().await?;
    let results = response.results.unwrap();

    assert_eq!(results.len(), 3, "{:?}", results);
    for result in results {
        assert_eq!(result.family.as_ref().unwrap().name, "Roboto");
    }
    Ok(())
}

async fn test_list_typefaces_by_name_substring() -> Result<(), Error> {
    let font_provider = get_provider(FONTS_MEDIUM_CM).await?;
    let (client, iterator) = create_proxy::<fonts_exp::ListTypefacesIteratorMarker>()?;

    let mut request = name_query("Noto");
    request.flags = Some(fonts_exp::ListTypefacesFlags::MatchFamilyNameSubstring);

    font_provider
        .list_typefaces(request, iterator.into())
        .await?
        .expect("ListTypefaces request failed");

    let response = client.get_next().await?;
    let results = response.results.unwrap();

    assert_eq!(results.len(), 14, "{:#?}", results);
    for result in results {
        assert!(result.family.as_ref().unwrap().name.contains("Noto"));
    }
    Ok(())
}

async fn test_list_typefaces_by_slant_range() -> Result<(), Error> {
    let font_provider = get_provider(FONTS_LARGE_CM).await?;
    let (client, iterator) = create_proxy::<fonts_exp::ListTypefacesIteratorMarker>()?;

    let request = slant_query(fonts::Slant::Upright, fonts::Slant::Italic);

    font_provider.list_typefaces(request, iterator).await?.expect("ListTypefaces request failed");

    let response = client.get_next().await?;
    let results = response.results.unwrap();

    assert!(!results.is_empty(), "{:#?}", results);
    for result in results {
        let slant = result.style.as_ref().unwrap().slant.unwrap();
        assert!((fonts::Slant::Upright..=fonts::Slant::Italic).contains(&slant));
    }
    Ok(())
}

async fn test_list_typefaces_by_slant_range_is_inclusive() -> Result<(), Error> {
    let font_provider = get_provider(FONTS_LARGE_CM).await?;
    let (client, iterator) = create_proxy::<fonts_exp::ListTypefacesIteratorMarker>()?;

    let request = slant_query(fonts::Slant::Italic, fonts::Slant::Italic);

    font_provider.list_typefaces(request, iterator).await?.expect("ListTypefaces request failed");

    let response = client.get_next().await?;
    let results = response.results.unwrap();

    assert!(!results.is_empty(), "{:?}", results);
    for result in results {
        let slant = result.style.as_ref().unwrap().slant.unwrap();
        assert_eq!(slant, fonts::Slant::Italic);
    }
    Ok(())
}

async fn test_list_typefaces_by_weight_range() -> Result<(), Error> {
    let font_provider = get_provider(FONTS_MEDIUM_CM).await?;
    let (client, iterator) = create_proxy::<fonts_exp::ListTypefacesIteratorMarker>()?;

    let request = weight_query(200, 300);

    font_provider.list_typefaces(request, iterator).await?.expect("ListTypefaces request failed");

    let response = client.get_next().await?;
    let results = response.results.unwrap();

    assert!(!results.is_empty(), "{:?}", results);
    for result in results {
        let weight = result.style.as_ref().unwrap().weight.unwrap();
        assert!((200..=300).contains(&weight));
    }
    Ok(())
}

async fn test_list_typefaces_by_weight_range_is_inclusive() -> Result<(), Error> {
    let font_provider = get_provider(FONTS_MEDIUM_CM).await?;
    let (client, iterator) = create_proxy::<fonts_exp::ListTypefacesIteratorMarker>()?;

    let request = weight_query(300, 300);

    font_provider.list_typefaces(request, iterator).await?.expect("ListTypefaces request failed");

    let response = client.get_next().await?;
    let results = response.results.unwrap();

    assert!(!results.is_empty(), "{:?}", results);
    for result in results {
        let weight = result.style.as_ref().unwrap().weight.unwrap();
        assert_eq!(weight, 300);
    }
    Ok(())
}

async fn test_list_typefaces_by_width_range() -> Result<(), Error> {
    let font_provider = get_provider(FONTS_MEDIUM_CM).await?;
    let (client, iterator) = create_proxy::<fonts_exp::ListTypefacesIteratorMarker>()?;

    let request = width_query(fonts::Width::Condensed, fonts::Width::Expanded);

    font_provider.list_typefaces(request, iterator).await?.expect("ListTypefaces request failed");

    let response = client.get_next().await?;
    let results = response.results.unwrap();

    assert!(!results.is_empty(), "{:?}", results);
    for result in results {
        let width = result.style.as_ref().unwrap().width.unwrap();
        assert!((fonts::Width::Condensed..=fonts::Width::Expanded).contains(&width));
    }
    Ok(())
}

async fn test_list_typefaces_by_width_range_is_inclusive() -> Result<(), Error> {
    let font_provider = get_provider(FONTS_MEDIUM_CM).await?;
    let (client, iterator) = create_proxy::<fonts_exp::ListTypefacesIteratorMarker>()?;

    let request = width_query(fonts::Width::Normal, fonts::Width::Normal);

    font_provider.list_typefaces(request, iterator).await?.expect("ListTypefaces request failed");

    let response = client.get_next().await?;
    let results = response.results.unwrap();

    assert!(!results.is_empty(), "{:?}", results);
    for result in results {
        let width = result.style.as_ref().unwrap().width.unwrap();
        assert_eq!(width, fonts::Width::Normal);
    }
    Ok(())
}

async fn test_list_typefaces_by_language() -> Result<(), Error> {
    let font_provider = get_provider(FONTS_MEDIUM_CM).await?;
    let (client, iterator) = create_proxy::<fonts_exp::ListTypefacesIteratorMarker>()?;

    let request = lang_query(vec![locale("ja")]);

    font_provider
        .list_typefaces(request, iterator.into())
        .await?
        .expect("ListTypefaces request failed");

    let response = client.get_next().await?;
    let results = response.results.unwrap();

    assert_eq!(results.len(), 3, "{:?}", results);
    for result in results {
        assert!(result.languages.unwrap().contains(&locale("ja")));
    }
    Ok(())
}

async fn test_list_typefaces_by_code_point() -> Result<(), Error> {
    let font_provider = get_provider(FONTS_MEDIUM_CM).await?;
    let (client, iterator) = create_proxy::<fonts_exp::ListTypefacesIteratorMarker>()?;

    let request = code_point_query(vec!['な' as u32]);

    font_provider
        .list_typefaces(request, iterator.into())
        .await?
        .expect("ListTypefaces request failed");

    let response = client.get_next().await?;
    let results = response.results.unwrap();

    assert!(!results.is_empty());
    for result in results {
        assert!(result.family.as_ref().unwrap().name.contains("Noto"));
    }
    Ok(())
}

async fn test_list_typefaces_by_generic_family() -> Result<(), Error> {
    let font_provider = get_provider(FONTS_MEDIUM_CM).await?;
    let (client, iterator) = create_proxy::<fonts_exp::ListTypefacesIteratorMarker>()?;

    let request = generic_family_query(fonts::GenericFontFamily::SansSerif);

    font_provider
        .list_typefaces(request, iterator.into())
        .await?
        .expect("ListTypefaces request failed");

    let response = client.get_next().await?;
    let results = response.results.unwrap();

    assert!(!results.is_empty());
    for result in results {
        assert_eq!(result.generic_family.as_ref().unwrap(), &fonts::GenericFontFamily::SansSerif);
    }
    Ok(())
}
