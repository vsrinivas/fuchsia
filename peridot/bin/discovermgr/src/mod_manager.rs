// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::models::{AddMod, Suggestion},
    failure::{format_err, Error},
    fidl_fuchsia_modular::{
        ExecuteStatus, FocusMod, PuppetMasterProxy, StoryCommand, StoryPuppetMasterMarker,
    },
    futures::future::join_all,
    std::collections::{HashMap, HashSet},
};

type EntityReference = String;

/// Starts mods and tracks the entity references in order to reissue intents
/// when entities change.
pub struct ModManager {
    // Maps an entity reference to actions launched with it.
    pub(super) actions: HashMap<EntityReference, HashSet<AddMod>>,
    puppet_master: PuppetMasterProxy,
}

impl ModManager {
    pub fn new(puppet_master: PuppetMasterProxy) -> Self {
        ModManager { actions: HashMap::new(), puppet_master }
    }

    pub async fn execute_suggestion(&mut self, suggestion: Suggestion) -> Result<(), Error> {
        await!(self.execute_actions(&suggestion.action(), /*focus=*/ true))?;
        for param in suggestion.action().intent().parameters() {
            self.actions
                .entry(param.entity_reference().to_string())
                .or_insert(HashSet::new())
                .insert(suggestion.action().clone());
        }
        Ok(())
    }

    pub async fn replace<'a>(&'a mut self, old: &'a str, new: &'a str) {
        let actions = self
            .actions
            .remove(old)
            .unwrap_or(HashSet::new())
            .into_iter()
            .map(|action| action.replace_reference_in_parameters(old, new))
            .collect::<HashSet<AddMod>>();
        await!(join_all(
            actions.iter().map(|action| self.execute_actions(action, /*focus=*/ false))
        ));
        self.actions.insert(new.to_string(), actions);
    }

    async fn execute_actions<'a>(&'a self, action: &'a AddMod, focus: bool) -> Result<(), Error> {
        let (story_puppet_master, server_end) =
            fidl::endpoints::create_proxy::<StoryPuppetMasterMarker>()?;
        self.puppet_master.control_story(&action.story_name(), server_end)?;
        let mut commands = vec![StoryCommand::AddMod(action.clone().into())];

        if focus {
            commands.push(StoryCommand::FocusMod(FocusMod {
                mod_name: vec![],
                mod_name_transitional: Some(action.mod_name.to_string()),
            }));
        }

        story_puppet_master.enqueue(&mut commands.iter_mut())?;
        let result = await!(story_puppet_master.execute())?;
        if result.status != ExecuteStatus::Ok {
            Err(format_err!(
                "Modular error status:{:?} message:{}",
                result.status,
                result.error_message.unwrap_or("none".to_string())
            ))
        } else {
            Ok(())
        }
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        crate::{
            models::{DisplayInfo, Intent},
            testing::puppet_master_fake::PuppetMasterFake,
        },
        fidl_fuchsia_modular::{IntentParameter, IntentParameterData, PuppetMasterMarker},
        fuchsia_async as fasync,
        maplit::{hashmap, hashset},
    };

    // Tests that the right action is executed for a suggestion.
    // For this Puppet master is mocked and on a call to execute the command
    // will be verified. It also ensures that the suggestion action is tracked
    // and associated to the entity reference for future re-issuance.
    #[fasync::run_singlethreaded(test)]
    async fn execute_suggestion() -> Result<(), Error> {
        // Setup puppet master fake.
        let (puppet_master_client, request_stream) =
            fidl::endpoints::create_proxy_and_stream::<PuppetMasterMarker>().unwrap();
        let mut puppet_master_fake = PuppetMasterFake::new();

        // This will be called when the suggestion is executed.
        puppet_master_fake.set_on_execute("story_name", |commands| {
            assert_eq!(commands.len(), 2);
            if let (StoryCommand::AddMod(add_mod), StoryCommand::FocusMod(focus_mod)) =
                (&commands[0], &commands[1])
            {
                assert_eq!(add_mod.intent.action, Some("PLAY_MUSIC".to_string()));
                assert_eq!(
                    add_mod.intent.parameters,
                    Some(vec![IntentParameter {
                        name: Some("artist".to_string()),
                        data: IntentParameterData::EntityReference("peridot-ref".to_string()),
                    },])
                );
                assert_eq!(focus_mod.mod_name_transitional, add_mod.mod_name_transitional);
            } else {
                assert!(false);
            }
        });

        puppet_master_fake.spawn(request_stream);
        let mut mod_manager = ModManager::new(puppet_master_client);

        let suggestion = suggestion!(
            action = "PLAY_MUSIC",
            title = "Play music",
            parameters = [(name = "artist", entity_reference = "peridot-ref")],
            story = "story_name"
        );
        let action = suggestion.action().clone();

        await!(mod_manager.execute_suggestion(suggestion))?;
        // Assert the action and the reference that originated it are tracked.
        assert_eq!(mod_manager.actions, hashmap!("peridot-ref".to_string() => hashset!(action)));

        Ok(())
    }

    // Tests that stored actions are issued with the udpated entity reference
    // when replace is called. The old action will be tracked and associated to
    // the new reference.
    // For this Puppet master is mocked and on a call to execute the command
    // will be verified.
    #[fasync::run_singlethreaded(test)]
    async fn replace() -> Result<(), Error> {
        // Setup puppet master fake.
        let (puppet_master_client, request_stream) =
            fidl::endpoints::create_proxy_and_stream::<PuppetMasterMarker>().unwrap();
        let mut puppet_master_fake = PuppetMasterFake::new();

        // This will be called with the action of the old reference but with
        // the replaced entity reference.
        puppet_master_fake.set_on_execute("story_name", |commands| {
            assert_eq!(commands.len(), 1);
            if let StoryCommand::AddMod(add_mod) = &commands[0] {
                assert_eq!(add_mod.intent.action, Some("PLAY_MUSIC".to_string()));
                assert_eq!(add_mod.mod_name_transitional, Some("mod_name".to_string()));
                assert_eq!(
                    add_mod.intent.parameters,
                    Some(vec![IntentParameter {
                        name: Some("artist".to_string()),
                        data: IntentParameterData::EntityReference("garnet-ref".to_string()),
                    },])
                );
            } else {
                assert!(false);
            }
        });

        puppet_master_fake.spawn(request_stream);
        let mut mod_manager = ModManager::new(puppet_master_client);

        // Set initial state. The actions here will be executed with the new
        // entity reference in the parameter.
        let intent = Intent::new().with_action("PLAY_MUSIC").add_parameter("artist", "peridot-ref");
        let action =
            AddMod::new(intent, Some("story_name".to_string()), Some("mod_name".to_string()));
        mod_manager.actions = hashmap!("peridot-ref".to_string() => hashset!(action));

        await!(mod_manager.replace("peridot-ref", "garnet-ref"));

        // Assert the action was replaced.
        let intent = Intent::new().with_action("PLAY_MUSIC").add_parameter("artist", "garnet-ref");
        let action =
            AddMod::new(intent, Some("story_name".to_string()), Some("mod_name".to_string()));
        assert_eq!(mod_manager.actions, hashmap!("garnet-ref".to_string() => hashset!(action)));

        Ok(())
    }
}
