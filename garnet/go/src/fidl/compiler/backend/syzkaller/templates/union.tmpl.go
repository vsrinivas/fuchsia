package templates

const Union = `
{{- define "UnionDefinition" -}}

{{ .Name }} [
       {{- range .Members }}
       {{ .Name }} {{ .Type }}
	   {{- end }}
] {{- if .VarLen -}} [varlen] {{- end -}}

{{- end -}}
`
