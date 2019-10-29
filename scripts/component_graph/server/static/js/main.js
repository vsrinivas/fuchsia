/**
 * @fileoverview Implements the interactive parsing of component data and
 * drawing it to the UI.
 */

/**
 * Simple wrapper around the component mapper json API.
 */
class ComponentMapperApi {
  constructor(baseUrl) {
    this.baseUrl = baseUrl;
    this.graphUrl = baseUrl + "api/component/graph";
  }

  /**
   * Returns the complete json graph of all components.
   */
  async getGraph() {
    let graph = await this.request(this.graphUrl);
    return graph;
  }

  /**
   * Private internal API that returns a promise to the JSON response
   */
  request(url) {
    return new Promise(function(resolve, reject) {
      const jsonRequest= new XMLHttpRequest();
      jsonRequest.open("GET", url);
      // Only accept 200 success.
      jsonRequest.onload = function() {
        if (this.status == 200) {
          resolve(JSON.parse(jsonRequest.response));
        } else {
          reject({status: this.status, statusText: jsonRequest.statusText});
        }
      }
      // Reject all errors.
      jsonRequest.onerror = function() {
        reject({status: this.status, statusText: jsonRequest.statusText});
      }
      jsonRequest.send();
    });
  }
}

/**
 * Simple class that renders the view for the component mapper.
 */
class ComponentMapperView {
  constructor(api) {
    this.api = api;
    this.graphData = null;
    this.search = document.getElementById("cm-search");
    this.search.addEventListener("input", this.autocompleteSearch);
    this.sidebar = document.getElementById("cm-component-list");
    this.map = document.getElementById("cm-map");
    this.logo = document.getElementById("cm-logo");

    document.body.onkeyup = (e) => {
      if (e.keyCode == 32) {
        this.resetGraphVisible();
      }
    }
  }

  /**
   * Does the basic setup for the view and populates all the important elements.
   */
  async init() {
    // Retrieve all the important view state.
    await this.refresh();
    // Draw the sidebar.
    this.drawSidebar(this.graphData["nodes"]);
    // Draw the main graph.
    this.drawGraph();
  }

  /**
   * Refresh all the apis and retrieve new packages etc.
   */
  async refresh() {
    console.log("[ComponentManager] - Refresh Triggered");
    this.graphData = await this.api.getGraph();
  }

  /**
   * Draws the main graph.
   */
  drawGraph(componentList) {
    console.log("[ComponentView] - Drawing Graph");
    const map = document.getElementById("cm-map");
    const boundingRect = map.getBoundingClientRect();
    const width = boundingRect.width;
    const height = boundingRect.height;
    const viewBox = `0 0 ${width} ${height}`;
    const nodeRadius = 120;
    const nodeBounding = 2*nodeRadius;

    // Construct the map.
    const svg = d3.select("#cm-map")
        .append("svg")
          .attr("preserveAspectRatio", "xMinYMin meet")
          .attr("viewBox", viewBox)
          .attr("width", width)
          .attr("height", height)
          .call(d3.zoom().on("zoom", function() {
            svg.attr("transform", d3.event.transform)
          }))
        .append("g");

    const view = this;
    // Construct the graph.
    d3.json(location.origin + '/api/component/graph').then(function(graph) {
        console.log("[ComponentView] - Parsing Graph JSON");

        const link = svg.append("g").selectAll("line")
          .data(graph.links)
          .enter()
          .append("line")
            .style("stroke", function(e) {
              return "#64B5F6";
            });

        const node = svg.append("g").selectAll("circle")
          .data(graph.nodes)
          .enter()
          .append("circle")
            .attr("r", function(e) {
              let calculatedRadius = nodeRadius + 5*e.consumers + 5*e.routes.uses.length;
              if (e.type == "core") {
                calculatedRadius = calculatedRadius*2;
              }
              e.radius = calculatedRadius;
              return calculatedRadius;
            })
            .style("fill", function(e) {
              if (e.source == "bootfs") {
                return "#FFA726";
              }
              if (e.source == "unknown") {
                return "#9C27B0";
              }
              if (e.routes.offers.length > 0) {
                return "#FF0266";
              }
              if (e.routes.uses.length > 0) {
                return "#1DE9B6";
              }
              return "#09A4AE";
            })
            .on("click", (e, i) => {
              const query = "^" + e.name + "$";
              view.search.value = query;
              view.updateSidebarWithSearchResults("^"+e.name+"$");
            });

        const label = svg.append("g").attr("class", "labels")
            .selectAll("text").data(graph.nodes).enter()
            .append("text")
            .attr("text-anchor","middle")
            .attr("class", "label")
            .style("font-size", function(e) {
              let calculatedSize = 20 + (e.consumers * 2) + (e.routes.uses.length * 2);
              if (e.type == "core") {
                calculatedSize = calculatedSize*2;
              }
              return calculatedSize;
            })
            .style("font-weight", "bold")
            .style("fill", "#e8eaed")
            .on("click", (e, i) => {
              const query = "^" + e.name + "$";
              view.search.value = query;
              view.updateSidebarWithSearchResults("^"+e.name+"$");
            })
            .text(function(e) { return e.name; });

        const simulate = d3.forceSimulation()
            .force("center", d3.forceCenter(width/2, height/2))
            .force("charge", d3.forceManyBody())
            .force("collide", d3.forceCollide().radius(function(e) { return 1.5* e.radius; }).iterations(1))
            .force("link", d3.forceLink().id(function(e) { return e.id; }));

        simulate
            .nodes(graph.nodes)
            .on("tick", function() {
              link
                .attr("x1", function(e) { return e.source.x; })
                .attr("y1", function(e) { return e.source.y; })
                .attr("x2", function(e) { return e.target.x; })
                .attr("y2", function(e) { return e.target.y; });

              node
                .attr("cx", function(e) { return e.x + 5 })
                .attr("cy", function(e) { return e.y - 3 });

              label
                  .attr("x", function(e) { return e.x })
                  .attr("y", function(e) { return e.y });
            })
            .on("end", function() {
              console.log("[ComponentView] Graph Stabalized");
            });
        simulate.force("link").links(graph.links);

        // Calculate what to do next.
    });
  }

  /**
   * Draws the component sidebar with the listed items.
   */
  drawSidebar(graphNodes) {
    this.sidebar.innerHTML = "";
    // TODO(benwright) - Move these to use CSS margins.
    for (const graphNode of graphNodes) {
      const listNode = document.createElement("li");
      listNode.appendChild(document.createTextNode(graphNode["name"]));
      if (graphNode["routes"]["uses"].length) {
        listNode.appendChild(document.createElement("br"));
        listNode.appendChild(document.createElement("br"));
        listNode.appendChild(document.createTextNode("uses:"));
        for (const use of graphNode["routes"]["uses"]) {
          listNode.appendChild(document.createElement("br"));
          listNode.appendChild(document.createTextNode(use));
        }
      }
      if (graphNode["routes"]["offers"].length) {
        listNode.appendChild(document.createElement("br"));
        listNode.appendChild(document.createElement("br"));
        listNode.appendChild(document.createTextNode("offers:"));
        for (const offer of graphNode["routes"]["offers"]) {
          listNode.appendChild(document.createElement("br"));
          listNode.appendChild(document.createTextNode(offer));
        }
      }
      listNode.appendChild(document.createElement("br"));
      listNode.appendChild(document.createElement("br"));
      listNode.appendChild(document.createTextNode("verson: " + graphNode["version"]));
      if (graphNode["source"]) {
        listNode.appendChild(document.createElement("br"));
        listNode.appendChild(document.createElement("br"));
        listNode.appendChild(document.createTextNode("source: " + graphNode["source"]));
      }
      if (graphNode["type"]) {
        listNode.appendChild(document.createElement("br"));
        listNode.appendChild(document.createElement("br"));
        listNode.appendChild(document.createTextNode("type: " + graphNode["type"]));
      }
      if (graphNode["consumers"]) {
        listNode.appendChild(document.createElement("br"));
        listNode.appendChild(document.createElement("br"));
        listNode.appendChild(document.createTextNode("consumers: " + graphNode["consumers"]));
      }

      this.sidebar.appendChild(listNode);
    }
  }

  /**
   * Hide components not related to the node.
   */
  hideGraphComponents(node) {
    this.resetGraphVisible();
    const componentName = node.name;
    const servicesOffered = node.routes.offers;
    d3.select('g').selectAll('circle').each(function(node) {
      if (node.name == componentName) {
        d3.select(this).style("stroke", "#ffaf49").style("stroke-width", "12");
        return;
      }
      const uses = servicesOffered.some(e => node.routes.uses.indexOf(e) >= 0);
      if (!uses) {
        d3.select(this).attr("visibility", "hidden");
      }
    });
    d3.select('g').selectAll('line').each(function(link) {
      if (link.source.name != componentName && link.target.name != componentName) {
        d3.select(this).attr("visibility", "hidden");
      }
    });
  }

  /**
   * Resets the graph back to the default state.
   */
  resetGraphVisible() {
    d3.select('g').selectAll('circle').each(function(node) {
      d3.select(this).attr("visibility", "visible");
        d3.select(this).style("stroke", "").style("stroke-width", "");
    });
    d3.select('g').selectAll('line').each(function(link) {
      d3.select(this).attr("visibility", "visible");
    });
  }

  /**
   * Completes the search to make the API usable.
   */
  autocompleteSearch = e => {
    this.updateSidebarWithSearchResults(e.target.value);
  }

  updateSidebarWithSearchResults(query) {
    if (this.graphData == null) {
      return;
    }
    const options = [];
    // Handle commands
      if (query.startsWith("uses: ")) {
        const subquery = query.split(" ")[1];
        for (const node of this.graphData["nodes"]) {
          for (const uses of node["routes"]["uses"]) {
            if (uses.match(subquery)) {
              options.push(node);
              break;
            }
          }
        }
      } else if (query.startsWith("offers: ")) {
        const subquery = query.split(" ")[1];
        for (const node of this.graphData["nodes"]) {
          for (const offers of node["routes"]["offers"]) {
            if (offers.match(subquery)) {
              options.push(node);
              break;
            }
          }
        }
      } else {
        for (const node of this.graphData["nodes"]) {
          if (node["name"].match(query)) {
            options.push(node);
          }
        }
    }
    if (query == "") {
      this.resetGraphVisible();
    } else if (options.length >= 1) {
      this.resetGraphVisible();
      d3.select('g').selectAll('circle').each(node => {
        if (node.name == options[0].name) {
          this.hideGraphComponents(node);
          return;
        }
      });
    } else {
      this.resetGraphVisible();
    }
    this.drawSidebar(options);
  }
}

window.onload = async function(e) {
  const api = new ComponentMapperApi(location.origin + '/');
  const view = new ComponentMapperView(api);
  view.init();
}
