package templates

const Struct = `
{{- define "StructDefinition" -}}

{{ .Name }} {
       {{- range .Members }}
       {{ .Name }} {{ .Type }}
       {{- end }}
}

{{- end -}}
`
