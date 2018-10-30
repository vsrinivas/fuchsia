# Regenerating Golden Files

    fx build garnet/go/src/fidl:fidlgen

## C++

    ./out/x64/host_x64/fidlgen \
        -json garnet/go/src/fidl/compiler/backend/typestest/doc_comments.fidl.json \
        -generators cpp \
        -output-base garnet/go/src/fidl/compiler/backend/typestest/doc_comments.fidl.json \
        -include-base garnet/go/src/fidl/compiler/backend/typestest
    mv garnet/go/src/fidl/compiler/backend/typestest/doc_comments.fidl.json.h \
        garnet/go/src/fidl/compiler/backend/typestest/doc_comments.fidl.json.h.golden
    mv garnet/go/src/fidl/compiler/backend/typestest/doc_comments.fidl.json.cc \
        garnet/go/src/fidl/compiler/backend/typestest/doc_comments.fidl.json.cc.golden

## Go

    ./out/x64/host_x64/fidlgen \
        -json garnet/go/src/fidl/compiler/backend/typestest/doc_comments.fidl.json \
        -generators go \
        -output-base garnet/go/src/fidl/compiler/backend/typestest \
        -include-base garnet/go/src/fidl/compiler/backend/typestest
    rm garnet/go/src/fidl/compiler/backend/typestest/pkg_name
    mv garnet/go/src/fidl/compiler/backend/typestest/impl.go \
        garnet/go/src/fidl/compiler/backend/typestest/doc_comments.fidl.json.go.golden
