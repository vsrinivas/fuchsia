// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package bloaty

import (
	"fmt"
	"html/template"
	"io"
)

const gchart = `
<html>
  <head>
    <title>{{.Title}}</title>
    <script type="text/javascript" src="https://www.gstatic.com/charts/loader.js"></script>
    <script type="text/javascript">
      google.charts.load('current', {'packages':['treemap']});
      google.charts.setOnLoadCallback(drawChart);
      function drawChart() {
        var data = google.visualization.arrayToDataTable({{.Data}});

        tree = new google.visualization.TreeMap(document.getElementById('chart_div'));

        var options = {
          highlightOnMouseOver: true,
          maxDepth: 0,
          maxPostDepth: 0,
          minHighlightColor: '#8c6bb1',
          midHighlightColor: '#9ebcda',
          maxHighlightColor: '#edf8fb',
          minColor: '#009688',
          midColor: '#f7f7f7',
          maxColor: '#ee8100',
          headerHeight: 15,
          showScale: true,
          useWeightedAverageForAggregation: true
        };

        tree.draw(data, options);
      }
    </script>
  </head>
  <body>
    <div id="chart_div" style="width: 100%; height: 100%;"></div>
  </body>
</html>`

type htmlData struct {
	Data  *[][]interface{}
	Title string
}

// Data format:
// Column 0 - [string] An ID for this node. It can be any valid JavaScript string,
//    including spaces, and any length that a string can hold. This value is
//    displayed as the node header.
// Column 1 - [string] - The ID of the parent node. If this is a root node,
//    leave this blank. Only one root is allowed per treemap.
// Column 2 - [number] - The size of the node. Any positive value is allowed.
//    This value determines the size of the node, computed relative to all other
//    nodes currently shown. For non-leaf nodes, this value is ignored and
//    calculated from the size of all its children.
func toTable(bloatyData map[string]*Segment) *[][]interface{} {
	var data [][]interface{}
	data = append(data, []interface{}{"ID", "Parent", "File Size", "Color"})
	data = append(data, []interface{}{"Build", nil, uint64(0), 0})

	for seg, segData := range bloatyData {
		data = append(data, []interface{}{seg, "Build", uint64(0), 0})
		for file, fileData := range segData.Files {
			data = append(data, []interface{}{fmt.Sprintf("%s (%s)", file, seg), seg, fileData.TotalFilesz, 0})
			for sym, symData := range fileData.Symbols {
				data = append(data, []interface{}{
					fmt.Sprintf("%s:%s (%s)", file, sym, seg),
					fmt.Sprintf("%s (%s)", file, seg),
					symData.Filesz,
					len(symData.Binaries),
				})
			}
		}
	}
	return &data
}

// Chart converts the map of symbols to the format expected by GChart, and then
// generates an HTML page from that data.
func Chart(bloatyData map[string]*Segment, title string, out io.Writer) error {
	temp, err := template.New("T").Parse(gchart)
	if err != nil {
		return err
	}
	err = temp.Execute(out, htmlData{toTable(bloatyData), title})
	if err != nil {
		return err
	}
	return nil
}
