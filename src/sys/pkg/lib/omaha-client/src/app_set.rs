// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{common::App, state_machine::update_check::AppResponse, storage::Storage},
    futures::{future::LocalBoxFuture, prelude::*},
};

/// The trait for the platform-specific AppSet to implement.
pub trait AppSet {
    fn get_apps(&self) -> Vec<App>;
    fn iter_mut_apps(&mut self) -> Box<dyn Iterator<Item = &mut App> + '_>;
    fn get_system_app_id(&self) -> &str;
}

pub trait AppSetExt: AppSet {
    /// Return whether all the apps in the app set are valid.
    fn all_valid(&self) -> bool {
        self.get_apps().iter().all(|app| app.valid())
    }

    /// Update the cohort and user counting for each app from Omaha app response.
    fn update_from_omaha(&mut self, app_responses: &[AppResponse]) {
        for app in self.iter_mut_apps() {
            for app_response in app_responses {
                if app.id == app_response.app_id {
                    app.cohort.update_from_omaha(app_response.cohort.clone());
                    app.user_counting = app_response.user_counting.clone();
                    break;
                }
            }
        }
    }

    /// Load data from |storage|, only overwrite existing fields if data exists.
    #[must_use]
    fn load<'a>(&'a mut self, storage: &'a impl Storage) -> LocalBoxFuture<'a, ()> {
        async move {
            for app in self.iter_mut_apps() {
                app.load(storage).await;
            }
        }
        .boxed_local()
    }

    /// Persist cohort and user counting to |storage|, will try to set all of them to storage even
    /// if previous set fails.
    /// It will NOT call commit() on |storage|, caller is responsible to call commit().
    #[must_use]
    fn persist<'a>(&'a self, storage: &'a mut impl Storage) -> LocalBoxFuture<'a, ()> {
        async move {
            for app in self.get_apps() {
                app.persist(storage).await;
            }
        }
        .boxed_local()
    }
}

impl<T> AppSetExt for T where T: AppSet {}

/// An AppSet implementation based on Vec, the first app will be treated as the system app.
pub struct VecAppSet {
    pub apps: Vec<App>,
}

impl VecAppSet {
    /// Panics if the passed apps is empty.
    pub fn new(apps: Vec<App>) -> Self {
        assert!(!apps.is_empty());
        Self { apps }
    }
}

impl AppSet for VecAppSet {
    fn get_apps(&self) -> Vec<App> {
        self.apps.clone()
    }
    fn iter_mut_apps(&mut self) -> Box<dyn Iterator<Item = &mut App> + '_> {
        Box::new(self.apps.iter_mut())
    }
    fn get_system_app_id(&self) -> &str {
        &self.apps[0].id
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::{common::UserCounting, protocol::Cohort, state_machine::update_check::Action};

    #[test]
    fn test_appsetext_update_from_omaha() {
        let mut app_set = VecAppSet::new(vec![
            App::builder().id("some_id").version([0, 1]).build(),
            App::builder().id("not_updated_id").version([2]).build(),
        ]);
        let cohort = Cohort { name: Some("some-channel".to_string()), ..Cohort::default() };
        let user_counting = UserCounting::ClientRegulatedByDate(Some(42));
        let app_responses = vec![
            AppResponse {
                app_id: "some_id".to_string(),
                cohort: cohort.clone(),
                user_counting: user_counting.clone(),
                result: Action::Updated,
            },
            AppResponse {
                app_id: "some_other_id".to_string(),
                cohort: cohort.clone(),
                user_counting: user_counting.clone(),
                result: Action::NoUpdate,
            },
        ];

        app_set.update_from_omaha(&app_responses);
        let apps = app_set.get_apps();
        assert_eq!(cohort, apps[0].cohort);
        assert_eq!(user_counting, apps[0].user_counting);

        assert_eq!(apps[1], App::builder().id("not_updated_id").version([2]).build());
    }

    #[test]
    fn test_appsetext_valid() {
        let app_set = VecAppSet::new(vec![App::builder().id("some_id").version([0, 1]).build()]);
        assert!(app_set.all_valid());
        let app_set = VecAppSet::new(vec![
            App::builder().id("some_id").version([0, 1]).build(),
            App::builder().id("some_id_2").version([1]).build(),
        ]);
        assert!(app_set.all_valid());
    }

    #[test]
    fn test_appsetext_not_valid() {
        let app_set = VecAppSet::new(vec![
            App::builder().id("some_id").version([0, 1]).build(),
            App::builder().id("").version([0, 1]).build(),
        ]);
        assert!(!app_set.all_valid());
        let app_set = VecAppSet::new(vec![
            App::builder().id("some_id").version([0]).build(),
            App::builder().id("some_id_2").version([0, 1]).build(),
        ]);
        assert!(!app_set.all_valid());
        let app_set = VecAppSet::new(vec![
            App::builder().id("some_id").version([0]).build(),
            App::builder().id("").version([0, 1]).build(),
        ]);
        assert!(!app_set.all_valid());
    }

    #[test]
    fn test_get_apps() {
        let apps = vec![App::builder().id("some_id").version([0, 1]).build()];
        let app_set = VecAppSet::new(apps.clone());
        assert_eq!(app_set.get_apps(), apps);
    }

    #[test]
    fn test_iter_mut_apps() {
        let apps = vec![
            App::builder().id("id1").version([1]).build(),
            App::builder().id("id2").version([2]).build(),
        ];
        let mut app_set = VecAppSet::new(apps);
        for app in app_set.iter_mut_apps() {
            app.id += "_mutated";
        }
        assert_eq!(
            app_set.get_apps(),
            vec![
                App::builder().id("id1_mutated").version([1]).build(),
                App::builder().id("id2_mutated").version([2]).build()
            ]
        );
    }

    #[test]
    fn test_get_system_app_id() {
        let apps = vec![
            App::builder().id("id1").version([1]).build(),
            App::builder().id("id2").version([2]).build(),
        ];
        let app_set = VecAppSet::new(apps);
        assert_eq!(app_set.get_system_app_id(), "id1");
    }
}
