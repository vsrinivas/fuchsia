const RECONNECT_INTERVAL = 500;
const MAX_RECONNECT_INTERVAL = 2000;

var _activeAskQueryFlag = false;
var _entities = {};
var _focusedStoryId = null;
var _modules = {};
var _reconnectInterval = RECONNECT_INTERVAL;
var _stories = {};
var _toolbar = null;
var _tabBar = null;
var _webSocket = null;

$(function() {
  mdc.autoInit();
  _toolbar =
    new mdc.toolbar.MDCToolbar(document.querySelector('.mdc-toolbar'));
  _tabBar =
    new mdc.tabs.MDCTabBar(document.querySelector('#dashboard-tab-bar'));

  _tabBar.listen('MDCTabBar:change', function (t) {
      var newPanelSelector = _tabBar.activeTab.root_.hash;
      updateTabPanel(newPanelSelector);
    });

  _tabBar.layout();

  connectWebSocket();
})

function updateTabPanel(newPanelSelector) {
  var activePanel = document.querySelector('.panel.active');
  if (activePanel) {
    activePanel.classList.remove('active');
  }
  var newActivePanel = document.querySelector(newPanelSelector);
  if (newActivePanel) {
    newActivePanel.classList.add('active');
  }
}

function connectWebSocket() {
  _webSocket = new WebSocket("ws://" + window.location.host + "/ws");
  _webSocket.onopen = handleWebSocketOpen;
  _webSocket.onerror = handleWebSocketError;
  _webSocket.onclose = handleWebSocketClose;
  _webSocket.onmessage = handleWebSocketMessage;
}

function handleWebSocketOpen(evt) {
  $("#connectedLabel").text("Connected");
  // reset reconnect
  _reconnectInterval = RECONNECT_INTERVAL;
}

function handleWebSocketError(evt) {
  console.log("WebSocket Error: " + evt.toString());
}

function handleWebSocketClose(evt) {
  $("#connectedLabel").text("Disconnected");

  // attempt to reconnect
  attemptReconnect();
}

function handleWebSocketMessage(evt) {
  // parse the JSON message
  var message = JSON.parse(evt.data);
  if ("context.update" in message) {
    handleContextUpdate(message["context.update"]);
  }
  if ("context.subscribers" in message) {
    handleContextSubscribers(message["context.subscribers"]);
  }
  if ("suggestions" in message) {
    handleSuggestionsUpdate(message["suggestions"]);
  }
  if ("action_log.all" in message) {
    handleActionLogReset(message["action_log.all"]);
  }
  if ("action_log.new_action" in message) {
    handleActionLogAdd(message["action_log.new_action"]);
  }
}

function makeProposalHtml(proposal) {
  var item = document.createElement("li");
  var headline = document.createElement("span");
  var subheadline = document.createElement("span");
  $(item).addClass("mdc-list-item").addClass("proposal-item");
  $(headline)
      .addClass("mdc-list-item__text")
      .text(proposal.display.headline);
  $(subheadline)
      .addClass("mdc-list-item__text__secondary")
      .text(proposal.display.subheadline);
  $(headline).append(subheadline);
  $(item).append(headline);
  return item;
}

function updateProposals(cardId, proposals) {
  // TODO(andrewosh): really should do some DOM diffing here.
  var cardList = $(cardId);
  cardList.empty();
  proposals.forEach(function (proposal) {
    cardList.append(makeProposalHtml(proposal));
  });
}

function updateLastSelection(selection) {
  var selectionList = $("#lastSelection");
  selectionList.empty();
  selectionList.append(makeProposalHtml(selection));
}

function updateLastQuery(query) {
  var queryElem = $("#lastQuery");
  queryElem.text(query);
}

function handleSuggestionsUpdate(suggestions) {
  updateProposals('#askProposals', suggestions.ask_proposals);
  updateProposals('#nextProposals', suggestions.next_proposals);
  if (_activeAskQueryFlag) {
    updateProposals('#askSuggestionsOverview', suggestions.ask_proposals);
  }
  if (suggestions.selection) {
    updateLastSelection(suggestions.selection);
  }
  updateLastQuery(suggestions.ask_query);
}

function handleContextUpdate(context) {
  updateOverviewFromContext(context);

  $.each(context, function(topic, rawValue) {
    // make a pretty string for the topic's value
    var topicValue;
    var danger;
    try {
      topicValue = JSON.stringify(JSON.parse(rawValue), null, 2);
      danger = false; // can't just leave it undefined;
                      // .toggleClass has a 1-arg overload
    } catch (e) {
      topicValue = rawValue;
      danger = true;
    }
    var topicId = "#topic-" + topic;
    var existingTopic = $("#" + $.escapeSelector(topicId));
    if (existingTopic.length > 0) {
      // element exists, update the value
      existingTopic.find("pre").text(topicValue)
        .toggleClass("text-danger", danger);
    } else {
      // element does not exist, add it to the table
      $("<tr/>").appendTo("#context")
        .append($("<td/>").addClass('wrappable').text(topic))
        .append($("<td/>").attr("id", topicId)
          .append(($("<pre/>").text(topicValue)
            .toggleClass("text-danger", danger))));
    }
  });
}

function handleContextSubscribers(subscribers) {
  $("#contextSubscriptions").empty();

  subscribers.forEach(function(update) {
    // Sample HTML to be created
    // <li class="mdc-list-item">
    //   <span class="mdc-list-item__text">
    //     <span class="agent-url">file:///system/apps/agents/module_suggester</span>
    //     <span class="mdc-list-item__text__secondary">raw/text</span>
    //   </span>
    // </li>
    // <li class="mdc-list-divider" role="separator"></li>
    var listText = $('<span/>').addClass('mdc-list-item__text');

    var agentNameText = update.subscriber.type;
    if (update.subscriber.url) {
      agentNameText += ' ' + update.subscriber.url;
    }
    if (update.subscriber.storyId) {
      agentNameText += ' from story ' + update.subscriber.storyId;
    }

    listText.append($("<span/>").addClass("subscriber")
                                .text(agentNameText));
    listText.append(' ').append(update.queries.map(function(query) {
      var querySpan = $('<span/>')
        .addClass('mdc-list-item__text__secondary')
        .addClass('context-topic');
      if (query.length == 0) {
        querySpan.append($('<span/>').text('(all topics)'));
      } else {
        query.forEach(function(topic) {
          querySpan.append($('<span/>').text(topic))
            .append(' ');
        })
      }
      return querySpan;
    }));

    $("#contextSubscriptions")
      .append($('<li/>').addClass('mdc-list-item').append(listText))
      .append($('<li/>').addClass('mdc-list-divider').attr('role','divider'));
  });
}

function handleActionLogReset(actionList) {
  $("#actionLog").empty();

  actionList.forEach(handleActionLogAdd);
}

function handleActionLogAdd(action) {
  // <li class="mdc-list-item">
  //   <span class="mdc-list-item__text">
  //     Two-line item
  //     <span class="mdc-list-item__text__secondary">Secondary text</span>
  //   </span>
  // </li>

  // Assemble Action Log Panel Data
  var methodElem = $("<span/>")
    .addClass('mdc-list-item__text')
    .text(action.method);
  var componentUrlElem = $("<span/>")
    .addClass('mdc-list-item__text__secondary')
    .text(action.component_url);
  var parametersElem = $("<span/>")
    .addClass('mdc-list-item__text__secondary')
    .text(action.parameters);

  methodElem.append(' ')
    .append(componentUrlElem)
    .append(' ')
    .append(parametersElem);

  $("#actionLog")
    .prepend($('<li/>').addClass('mdc-list-item').append(methodElem))
    .prepend($('<li/>').addClass('mdc-list-divider').attr('role','divider'));

  // Assemble Action Log Overview Data
  methodElem = $('<span/>')
    .addClass('mdc-list-item__text')
    .text(action.method);
  parametersElem = $("<span/>")
    .addClass('mdc-list-item__text__secondary')
    .text(action.parameters);

  methodElem.append(" ").append(parametersElem);
  $('#actionLogOverview')
    .prepend($('<li/>').addClass('mdc-list-item').append(methodElem))
    .prepend($('<li/>').addClass('mdc-list-divider').attr('role','divider'));
}

function attemptReconnect() {
  console.log("Attempting to reconnect after " + _reconnectInterval);

  // reconnect after the timeout
  setTimeout(connectWebSocket, _reconnectInterval);
  // exponential reconnect timeout
  var nextInterval = _reconnectInterval * 2;
  if (nextInterval < MAX_RECONNECT_INTERVAL) {
    _reconnectInterval = nextInterval;
  } else {
    _reconnectInterval = MAX_RECONNECT_INTERVAL;
  }
}

function updateOverviewFromContext(context) {
  $.each(context, function(topic, rawValue) {
    // loop through the context updates and modify anything related
    // on the overview panel
    var storyRegex = /\/story\/id\/([^\/]+)\/(.+)/;
    var storyRegexResults = topic.match(storyRegex);

    if (storyRegexResults != null && storyRegexResults[1] != null) {
      // this is a story-related topic
      var storyId = storyRegexResults[1];
      var overviewStoryElems = getOrCreateStoryOverviewElements(storyId);

      switch(storyRegexResults[2]) {
        case "url":
          setStoryName(JSON.parse(rawValue), overviewStoryElems);
          break;

        case "state":
          setStoryState(rawValue, overviewStoryElems);
          break;

        default:
          processComplexStoryTopic(storyId,storyRegexResults[2],rawValue);
          break;
      }

      // move this story to the top of the list
      $('#story-overview-list')
        .prepend(overviewStoryElems);
    } else if (topic == '/story/focused_id') {
      if (_focusedStoryId != null) {
        var oldFocusedStoryElems = getOrCreateStoryOverviewElements(_focusedStoryId);
        setStoryVisible(false, oldFocusedStoryElems);
      }
      var newFocusedStoryId = JSON.parse(rawValue);
      if (newFocusedStoryId != null) {
        var newFocusedStoryElems = getOrCreateStoryOverviewElements(newFocusedStoryId);
        setStoryVisible(true, newFocusedStoryElems);
      }
      _focusedStoryId = newFocusedStoryId;
    } else if (topic == '/suggestion_engine/current_query') {
      var query = JSON.parse(rawValue);
      _activeAskQueryFlag = (query.length > 0);
      if (_activeAskQueryFlag) {
        $('#askQueryOverview').empty()
          .append($('<span/>').addClass('ask-query').text('"' + query + '"'));
      } else {
        $('#askQueryOverview').empty().append($('<i/>').text('No Query'));
        $('#askSuggestionsOverview').empty();
      }
    }
  });
}

function processComplexStoryTopic(storyId,complexTopic,rawValue) {
  var moduleRegex = /module\/([^\/]+)\/(.+)/;
  var moduleRegexResults = complexTopic.match(moduleRegex);

  if (moduleRegexResults != null && moduleRegexResults[1] != null) {
    var moduleHash = moduleRegexResults[1];
    var moduleTopic = moduleRegexResults[2];

    if (moduleTopic == 'meta') {
      // This topic contains information about the module
      updateModuleInStory(storyId,moduleHash,rawValue);
    } else if (moduleTopic.startsWith('explicit')) {
      // This topic contains informationa about a focal entity
      updateFocalEntityInStory(storyId,moduleHash,moduleTopic,rawValue);
    }
  }
}

function updateModuleInStory(storyId,moduleHash,rawData) {
  var moduleData = JSON.parse(rawData);
  var moduleElems = getOrCreateModuleOverviewElements(storyId,moduleHash);
  if (moduleData['url'] != null) {
    setModuleUrl(moduleData['url'],moduleElems);
  }
  if (moduleData['module_path'] != null) {
    var modulePath = moduleData['module_path'][0];
    for(var i = 1; i < moduleData['module_path'].length; i++) {
      modulePath += ' ' + moduleData['module_path'][i];
    }
    setModulePath(modulePath,moduleElems);
  }
}

function getOrCreateModuleOverviewElements(storyId,moduleHash) {
  var moduleElemId = storyId + '-' + moduleHash;
  var moduleElems = _modules[moduleElemId];
  if (moduleElems == null) {
    // <li id="b234kj2jn5j34342l3k3-mdece" class="mdc-list-item module-list-item">
    //   <span class="module-url mdc-list-item__text">
    //     file:///system/apps/maxwell_btr
    //     <span class="module-path mdc-list-item__text__secondary">
    //       root
    //     </span>
    //   </span>
    // </li>
    var moduleListElem = $('<li/>').attr('id',moduleElemId)
      .addClass('mdc-list-item')
      .addClass('module-list-item');

    var modulePathElem = $('<span/>').addClass('module-path')
      .addClass('mdc-list-item__text__secondary')
      .text('module-path');

    var moduleUrlElem = $('<span/>').addClass('module-url')
      .addClass('mdc-list-item__text')
      .text('file://a/module/url')
      .append(' ')
      .append(modulePathElem);

    moduleListElem.append(moduleUrlElem);

    var storyElems = getOrCreateStoryOverviewElements(storyId);
    storyElems.append(moduleListElem);

    moduleElems = _modules[moduleElemId] = moduleListElem;
  }
  return moduleElems;
}

function setModuleUrl(moduleUrl,moduleElems) {
  moduleElems.find('span')[0].firstChild.textContent = moduleUrl;
}

function setModulePath(modulePath,moduleElems) {
  moduleElems.find('span')[1].firstChild.textContent = modulePath;
}

function updateFocalEntityInStory(storyId,moduleHash,entityTopic,rawValue) {
  var entityTopicRegex = /explicit\/(.+)/;
  var entityTopicRegexResults = entityTopic.match(entityTopicRegex);

  if (entityTopicRegexResults != null && entityTopicRegexResults[1] != null) {
    var entityElems = getOrCreateEntityOverviewElements(storyId,
                        moduleHash,
                        entityTopicRegexResults[1]);
    setEntityValue(rawValue,entityElems);
  }
}

function getOrCreateEntityOverviewElements(storyId,moduleHash,entityTopic) {
  var entityElemId = storyId + '-' + moduleHash + '-' + entityTopic;
  var entityElems = _entities[entityElemId];
  if (entityElems == null) {
    // <li id="b234kj2jn5j34342l3k3-mdece-raw/text" class="mdc-list-item entity-list-item">
    //   <span class="entity-name mdc-list-item__text">
    //     raw/text
    //     <span class="entity-value mdc-list-item__text__secondary">
    //       TODO: value
    //     </span>
    //   </span>
    // </li>
    var entityListElem = $('<li/>').attr('id',entityElemId)
      .addClass('mdc-list-item')
      .addClass('entity-list-item');

    var entityValueElem = $('<span/>').addClass('entity-value')
      .addClass('mdc-list-item__text__secondary')
      .text('entity value');

    var entityNameElem = $('<span/>').addClass('entity-name')
      .addClass('mdc-list-item__text')
      .text(entityTopic)
      .append(' ')
      .append(entityValueElem);

    entityListElem.append(entityNameElem);

    var moduleElems = getOrCreateModuleOverviewElements(storyId,moduleHash);
    entityListElem.insertAfter(moduleElems);

    entityElems = _entities[entityElemId] = entityListElem;
  }
  return entityElems;
}

function setEntityName(entityName,entityElems) {
  entityElems.find('span')[0].firstChild.textContent = entityName;
}

function setEntityValue(entityValue,entityElems) {
  entityElems.find('span')[1].firstChild.textContent = entityValue;
}

function getOrCreateStoryOverviewElements(storyId) {
  var storyElems = _stories[storyId];
  if (storyElems == null) {
    // we need to create the story elements
    // <div class="story-list-group">
    // <li id="b234kj2jn5j34342l3k3" class="mdc-list-item story-list-item story-visible">
    //   <span class="story-name mdc-list-item__text">
    //     Story Name
    //     <span class="story-id mdc-list-item__text__secondary">
    //       b234kj2jn5j34342l3k3
    //     </span>
    //   </span>
    // </li>
    // </div>
    var storyDivItem = $('<div/>').addClass('story-list-group')
      .attr('id',storyId);

    var storyListItem = $('<li/>').addClass('mdc-list-item')
      .addClass('story-list-item');

    var storyIdElem = $('<span/>').addClass('story-id')
      .addClass('mdc-list-item__text__secondary')
      .text(storyId);

    var storyNameElem = $('<span/>').addClass('story-name')
      .addClass('mdc-list-item__text')
      .text('Story Name')
      .append(' ')
      .append(storyIdElem);

    var divider = $('<li/>').addClass('mdc-list-divider').attr('role','divider');

    storyListItem.append(storyNameElem);
    storyDivItem.append(divider)
      .append(storyListItem);

    $('#story-overview-list').append(storyDivItem);

    storyElems = _stories[storyId] = storyDivItem;
  }
  return storyElems;
}

function setStoryName(storyName, storyElems) {
  storyElems.find('span')[0].firstChild.textContent = storyName;
}

function setStoryVisible(storyVisible, storyElems) {
  if (storyVisible) {
    storyElems.addClass('story-visible');
  } else {
    storyElems.removeClass('story-visible');
  }
}

function setStoryState(storyState, storyElems) {
  // TODO(jwnichols): Add story state to the overview
}
