# Copyright 2015 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import unittest
import module

try:
  import mojom_translator
  from generated import mojom_files_mojom
  from generated import mojom_types_mojom
  bindings_imported = True
except ImportError:
  bindings_imported = False


@unittest.skipUnless(bindings_imported, 'Could not import python bindings.')
class TranslateFileGraph(unittest.TestCase):

  def test_basics(self):
    g = mojom_files_mojom.MojomFileGraph()

    # File names need to be set so the file can be translated at all.
    g.files = {
        'a.mojom': mojom_files_mojom.MojomFile(
            file_name='a.mojom',
            specified_file_name='',
            imports=[]),
        'b.mojom': mojom_files_mojom.MojomFile(
            file_name='b.mojom',
            specified_file_name='',
            imports=[]),
        'root/c.mojom': mojom_files_mojom.MojomFile(
            file_name='root/c.mojom',
            specified_file_name='',
            imports=[]),
    }

    modules = mojom_translator.TranslateFileGraph(g)
    self.assertEquals(len(modules), len(g.files))


@unittest.skipUnless(bindings_imported, 'Could not import python bindings.')
class TestTranslateFile(unittest.TestCase):

  def test_basics(self):
    graph = mojom_files_mojom.MojomFileGraph(
        resolved_types={})

    file_name = 'root/f.mojom'
    imported_file_name = 'other/a.mojom'
    second_level_imported_file_name = 'something/other.mojom'
    mojom_file = mojom_files_mojom.MojomFile(
        file_name=file_name,
        specified_file_name='specified_file_name',
        module_namespace='somens',
        imports=[imported_file_name])
    imported_file = mojom_files_mojom.MojomFile(
        file_name=imported_file_name,
        specified_file_name='',
        module_namespace='somens',
        imports=[second_level_imported_file_name])
    second_level_imported_file = mojom_files_mojom.MojomFile(
        file_name=second_level_imported_file_name,
        specified_file_name='',
        module_namespace='somens')
    graph.files = {
        file_name: mojom_file,
        imported_file_name: imported_file,
        second_level_imported_file_name: second_level_imported_file
        }

    mojom_interface = mojom_types_mojom.MojomInterface(
        methods={},
        decl_data=mojom_types_mojom.DeclarationData(
          short_name='AnInterface',
          source_file_info=mojom_types_mojom.SourceFileInfo(
            file_name=file_name)))
    graph.resolved_types['interface_key'] = mojom_types_mojom.UserDefinedType(
        interface_type=mojom_interface)

    mojom_struct = mojom_types_mojom.MojomStruct(
        fields=[],
        decl_data=mojom_types_mojom.DeclarationData(
          short_name='AStruct',
          full_identifier='foo.AStruct',
          source_file_info=mojom_types_mojom.SourceFileInfo(
            file_name=file_name)))
    add_version_info(mojom_struct, 0)
    graph.resolved_types['struct_key'] = mojom_types_mojom.UserDefinedType(
        struct_type=mojom_struct)

    mojom_union = mojom_types_mojom.MojomUnion(
        fields=[],
        decl_data=mojom_types_mojom.DeclarationData(
          short_name='AUnion',
          source_file_info=mojom_types_mojom.SourceFileInfo(
            file_name=file_name)))
    graph.resolved_types['union_key'] = mojom_types_mojom.UserDefinedType(
        union_type=mojom_union)

    mojom_enum = mojom_types_mojom.MojomEnum(
        values=[],
        decl_data=mojom_types_mojom.DeclarationData(
          short_name='AnEnum',
          source_file_info=mojom_types_mojom.SourceFileInfo(
            file_name=file_name)))
    graph.resolved_types['enum_key'] = mojom_types_mojom.UserDefinedType(
        enum_type=mojom_enum)

    mojom_const = mojom_types_mojom.DeclaredConstant(
        decl_data=mojom_types_mojom.DeclarationData(short_name='AConst'),
        type=mojom_types_mojom.Type(
          simple_type=mojom_types_mojom.SimpleType.INT64),
        value=mojom_types_mojom.Value(
          literal_value=mojom_types_mojom.LiteralValue(
            int64_value=30)))
    graph.resolved_constants = {'constant_key': mojom_const}

    mojom_file.declared_mojom_objects = mojom_files_mojom.KeysByType(
        interfaces=['interface_key'],
        structs=['struct_key'],
        unions=['union_key'],
        top_level_enums=['enum_key'],
        top_level_constants=['constant_key']
        )

    mod = mojom_translator.FileTranslator(graph, file_name).Translate()

    self.assertEquals('f.mojom', mod.name)
    self.assertEquals(mojom_file.specified_file_name, mod.specified_name)
    self.assertEquals(mojom_file.file_name, mod.path)
    self.assertEquals(mojom_file.module_namespace, mod.namespace)

    self.assertEquals(1, len(mod.imports))
    self.assertEquals('a.mojom', mod.imports[0]['module_name'])
    self.assertEquals(imported_file.module_namespace,
        mod.imports[0]['namespace'])
    self.assertEquals(imported_file.file_name, mod.imports[0]['module'].path)

    self.assertEquals(2, len(mod.transitive_imports))
    transitive_imports_paths = [imp['module'].path
        for imp in mod.transitive_imports]
    self.assertIn(imported_file_name, transitive_imports_paths)
    self.assertIn(second_level_imported_file_name, transitive_imports_paths)

    self.assertEquals('AnInterface', mod.interfaces[0].name)
    # Interfaces should be assigned their name as their spec.
    self.assertEquals('AnInterface', mod.interfaces[0].spec)
    self.assertEquals(mojom_struct.decl_data.short_name, mod.structs[0].name)
    # The struct was given a full_identifier so its spec should be that.
    self.assertEquals(mojom_struct.decl_data.full_identifier,
        mod.structs[0].spec)
    self.assertEquals(mojom_union.decl_data.short_name, mod.unions[0].name)
    # The union was given a short name but not a full_identifier so its spec
    # should be the short name.
    self.assertEquals(mojom_union.decl_data.short_name,
        mod.unions[0].spec)
    self.assertEquals(mojom_enum.decl_data.short_name, mod.enums[0].name)
    self.assertEquals(mojom_const.decl_data.short_name, mod.constants[0].name)

    imported_mod = mojom_translator.FileTranslator(
        graph, imported_file_name).Translate()
    self.assertFalse(imported_mod.specified_name)

  def test_no_imports(self):
    graph = mojom_files_mojom.MojomFileGraph(
        resolved_types={})
    file_name = 'root/f.mojom'
    mojom_file = mojom_files_mojom.MojomFile(
        file_name=file_name,
        specified_file_name='',
        module_namespace='somens')
    graph.files = { file_name: mojom_file }

    # Should not throw exceptions despite imports not being set on the file.
    mod = mojom_translator.FileTranslator(graph, file_name).Translate()

    self.assertEquals([], mod.imports)

@unittest.skipUnless(bindings_imported, 'Could not import python bindings.')
class TestUserDefinedFromTypeRef(unittest.TestCase):

  def do_interface_test(self, nullable, interface_request):
    # Build a MojomInterface
    file_name = 'a.mojom'
    mojom_interface = mojom_types_mojom.MojomInterface(
        decl_data=mojom_types_mojom.DeclarationData(
          short_name='AnInterface',
          source_file_info=mojom_types_mojom.SourceFileInfo(
            file_name=file_name)))
    mojom_interface.methods={}

    # Register the MojomInterface in a MojomFileGraph
    graph = mojom_files_mojom.MojomFileGraph()
    type_key = 'some_type_key'
    graph.resolved_types = {
      type_key: mojom_types_mojom.UserDefinedType(
        interface_type=mojom_interface)}

    # Build a reference to the interface.
    type_ref = mojom_types_mojom.Type(
        type_reference=mojom_types_mojom.TypeReference(
            type_key=type_key,
            nullable=nullable,
            is_interface_request=interface_request))

    # Construct a translator
    translator = mojom_translator.FileTranslator(graph, file_name)

    # Translate the MojomInterface referenced by type_ref.
    interface = translator.UserDefinedFromTypeRef(type_ref)

    # Check the translation
    if interface_request:
      self.assertEquals('AnInterface', interface.kind.name)
      self.assertEquals('some_type_key', interface.kind.type_key)
    else:
      self.assertEquals('AnInterface', interface.name)
      self.assertEquals('some_type_key', interface.type_key)
    self.assertEquals(nullable, interface.is_nullable)
    self.assertEquals(interface_request, isinstance(interface,
        module.InterfaceRequest))

  def test_interfaces(self):
    self.do_interface_test(False, False)
    self.do_interface_test(False, True)
    self.do_interface_test(True, False)
    self.do_interface_test(True, True)

@unittest.skipUnless(bindings_imported, 'Could not import python bindings.')
class TestUserDefinedTypeFromMojom(unittest.TestCase):

  def test_structs(self):
    file_name = 'a.mojom'
    graph = mojom_files_mojom.MojomFileGraph()
    mojom_file = mojom_files_mojom.MojomFile(
        file_name='a.mojom',
        module_namespace='foo.bar')
    graph.files = {mojom_file.file_name: mojom_file}

    mojom_struct = mojom_types_mojom.MojomStruct(
        decl_data=mojom_types_mojom.DeclarationData(short_name='FirstStruct'))
    mojom_struct.fields = [
       mojom_types_mojom.StructField(
          decl_data=mojom_types_mojom.DeclarationData(
            short_name='field03',
            declaration_order=2),
          type=mojom_types_mojom.Type(
            simple_type=mojom_types_mojom.SimpleType.BOOL),
          offset=21,
          bit=6,
          min_version=11),
        mojom_types_mojom.StructField(
          decl_data=mojom_types_mojom.DeclarationData(
            short_name='field01',
            declared_ordinal=1,
            declaration_order=0),
          type=mojom_types_mojom.Type(
            simple_type=mojom_types_mojom.SimpleType.BOOL),
          offset=17,
          bit=1,
          min_version=4),
        mojom_types_mojom.StructField(
          decl_data=mojom_types_mojom.DeclarationData(
            short_name='field02',
            declaration_order=1),
          type=mojom_types_mojom.Type(
            simple_type=mojom_types_mojom.SimpleType.DOUBLE),
          offset=0,
          bit=0,
          min_version=0,
          default_value=mojom_types_mojom.DefaultFieldValue(
            value=mojom_types_mojom.Value(
              literal_value=mojom_types_mojom.LiteralValue(double_value=15)))),
        ]
    mojom_struct.version_info=[
        mojom_types_mojom.StructVersion(
            version_number=0, num_bytes=67, num_fields=1),
         mojom_types_mojom.StructVersion(
            version_number=1, num_bytes=76, num_fields=3),
    ]
    # mojom_fields_declaration_order lists, in declaration order, the indices
    # of the fields in mojom_types_mojom.StructField.
    mojom_fields_declaration_order = [1, 2, 0]
    mojom_struct.decl_data.source_file_info = mojom_types_mojom.SourceFileInfo(
        file_name=mojom_file.file_name)

    struct = module.Struct()
    translator = mojom_translator.FileTranslator(graph, file_name)
    translator.StructFromMojom(
        struct, mojom_types_mojom.UserDefinedType(struct_type=mojom_struct))

    self.assertEquals('FirstStruct', struct.name)
    self.assertEquals(translator._module, struct.module)

    self.assertEquals(len(mojom_struct.fields), len(struct.fields))
    for index, gold_index in enumerate(mojom_fields_declaration_order):
      gold = mojom_struct.fields[gold_index]
      f = struct.fields[index]
      self.assertEquals(f.name, gold.decl_data.short_name)
      if gold.decl_data.declared_ordinal >= 0:
        self.assertEquals(gold.decl_data.declared_ordinal, f.ordinal)
      else:
        self.assertEquals(None, f.ordinal)
      self.assertEquals(gold_index, f.computed_ordinal)
      self.assertEquals(gold.offset, f.computed_offset)
      self.assertEquals(gold.bit, f.computed_bit)
      self.assertEquals(gold.min_version, f.computed_min_version)
      self.assertEquals(struct.fields_in_ordinal_order[index].name,
          mojom_struct.fields[index].decl_data.short_name)

    self.assertEquals(2, len(struct.versions))
    for i in xrange(0, 2):
      self.assertEquals(mojom_struct.version_info[i].version_number,
          struct.versions[i].version)
      self.assertEquals(mojom_struct.version_info[i].num_bytes,
          struct.versions[i].num_bytes)
      self.assertEquals(mojom_struct.version_info[i].num_fields,
          struct.versions[i].num_fields)

    self.assertEquals(module.BOOL, struct.fields[0].kind)
    self.assertEquals(module.DOUBLE, struct.fields[1].kind)

    self.assertEquals('15.0', struct.fields[1].default)

  def test_constant(self):
    file_name = 'a.mojom'
    graph = mojom_files_mojom.MojomFileGraph()

    mojom_const = mojom_types_mojom.DeclaredConstant()
    mojom_const.decl_data = mojom_types_mojom.DeclarationData(
        short_name='foo', container_type_key='struct_key')
    mojom_const.type = mojom_types_mojom.Type(
        simple_type=mojom_types_mojom.SimpleType.INT64)
    mojom_const.value = mojom_types_mojom.Value()
    mojom_const.value.literal_value = mojom_types_mojom.LiteralValue(
        int64_value=20)

    mojom_struct = mojom_types_mojom.MojomStruct(
        fields=[],
        decl_data=mojom_types_mojom.DeclarationData(
          short_name='AStruct',
          source_file_info =mojom_types_mojom.SourceFileInfo(
            file_name=file_name)))
    add_version_info(mojom_struct, 0)
    graph.resolved_types = {'struct_key': mojom_types_mojom.UserDefinedType(
      struct_type=mojom_struct)}

    const = module.Constant()
    translator = mojom_translator.FileTranslator(graph, file_name)
    translator.ConstantFromMojom(const, mojom_const)

    self.assertEquals(mojom_const.decl_data.short_name, const.name)
    self.assertEquals(module.INT64, const.kind)
    self.assertEquals('20', const.value)
    self.assertEquals(translator.UserDefinedFromTypeKey('struct_key'),
        const.parent_kind)

  def test_enum(self):
    file_name = 'a.mojom'
    mojom_enum = mojom_types_mojom.MojomEnum()
    mojom_enum.decl_data = mojom_types_mojom.DeclarationData(
        short_name='AnEnum',
        source_file_info=mojom_types_mojom.SourceFileInfo(file_name=file_name))
    value1 = mojom_types_mojom.EnumValue(
        decl_data=mojom_types_mojom.DeclarationData(short_name='val1'),
        initializer_value=mojom_types_mojom.Value(
            literal_value=mojom_types_mojom.LiteralValue(uint64_value=20)),
        int_value=20)
    value2 = mojom_types_mojom.EnumValue(
        decl_data=mojom_types_mojom.DeclarationData(short_name='val2'),
        int_value=70)
    mojom_enum.values = [value1, value2]


    graph = mojom_files_mojom.MojomFileGraph()
    enum = module.Enum()
    translator = mojom_translator.FileTranslator(graph, file_name)
    translator.EnumFromMojom(
        enum, mojom_types_mojom.UserDefinedType(enum_type=mojom_enum))

    self.assertEquals(translator._module, enum.module)
    self.assertEquals(mojom_enum.decl_data.short_name, enum.name)
    self.assertEquals(len(mojom_enum.values), len(enum.fields))

    self.assertEquals(value1.decl_data.short_name, enum.fields[0].name)
    self.assertEquals(value2.decl_data.short_name, enum.fields[1].name)

    self.assertEquals('20', enum.fields[0].value)
    self.assertIsNone(enum.fields[1].value)

    self.assertEquals(value1.int_value,
        enum.fields[0].numeric_value)
    self.assertEquals(value2.int_value,
        enum.fields[1].numeric_value)

  def test_child_enum(self):
    file_name = 'a.mojom'
    mojom_enum = mojom_types_mojom.MojomEnum()
    mojom_enum.decl_data = mojom_types_mojom.DeclarationData(
        short_name='AnEnum',
        source_file_info=mojom_types_mojom.SourceFileInfo(file_name=file_name),
        container_type_key='struct_key')
    mojom_enum.values = []

    graph = mojom_files_mojom.MojomFileGraph()
    mojom_struct = mojom_types_mojom.MojomStruct(
        fields=[],
        decl_data=mojom_types_mojom.DeclarationData(
          short_name='AStruct',
          source_file_info =mojom_types_mojom.SourceFileInfo(
            file_name=file_name)))
    add_version_info(mojom_struct, 0)
    graph.resolved_types = {'struct_key': mojom_types_mojom.UserDefinedType(
      struct_type=mojom_struct)}

    enum = module.Enum()
    translator = mojom_translator.FileTranslator(graph, file_name)
    translator.EnumFromMojom(
        enum, mojom_types_mojom.UserDefinedType(enum_type=mojom_enum))

    self.assertEquals(mojom_enum.decl_data.short_name, enum.name)
    self.assertEquals(len(mojom_enum.values), len(enum.fields))

  def test_unions(self):
    file_name = 'a.mojom'
    mojom_union = mojom_types_mojom.MojomUnion()
    mojom_union.decl_data = mojom_types_mojom.DeclarationData(
        short_name='AUnion',
        source_file_info=mojom_types_mojom.SourceFileInfo(file_name=file_name))

    field1 = mojom_types_mojom.UnionField(
        decl_data=mojom_types_mojom.DeclarationData(short_name='field1',
            declaration_order=0, declared_ordinal=7),
        type=mojom_types_mojom.Type(
          simple_type=mojom_types_mojom.SimpleType.BOOL),
        tag=7)
    field2 = mojom_types_mojom.UnionField(
        decl_data=mojom_types_mojom.DeclarationData(
            short_name='field2', declaration_order=1),
        type=mojom_types_mojom.Type(
          simple_type=mojom_types_mojom.SimpleType.DOUBLE),
        tag=8)
    field3 = mojom_types_mojom.UnionField(
        decl_data=mojom_types_mojom.DeclarationData(short_name='field3',
            declaration_order=2, declared_ordinal=0),
        type=mojom_types_mojom.Type(
          simple_type=mojom_types_mojom.SimpleType.INT32),
        tag=0)

    mojom_union.fields = [field3, field1, field2]
    # mojom_fields_declaration_order lists, in declaration order, the indices
    # of the fields in mojom_union.fields
    mojom_fields_declaration_order = [1, 2, 0]

    graph = mojom_files_mojom.MojomFileGraph()
    union = module.Union()
    translator = mojom_translator.FileTranslator(graph, file_name)
    translator.UnionFromMojom(
        union, mojom_types_mojom.UserDefinedType(union_type=mojom_union))

    self.assertEquals(translator._module, union.module)
    self.assertEquals('AUnion', union.name)
    self.assertEquals(len(mojom_union.fields), len(union.fields))

    for index, gold_index in enumerate(mojom_fields_declaration_order):
        gold = mojom_union.fields[gold_index]
        f = union.fields[index]
        self.assertEquals(gold.decl_data.short_name, f.name)
        if gold.decl_data.declared_ordinal >= 0:
          self.assertEquals(gold.decl_data.declared_ordinal, f.declared_tag)
        else:
          self.assertEquals(None, f.declared_tag)
        self.assertEquals(gold.tag, f.ordinal)

    self.assertEquals(module.BOOL, union.fields[0].kind)
    self.assertEquals(module.DOUBLE, union.fields[1].kind)
    self.assertEquals(module.INT32, union.fields[2].kind)

  def literal_value(self, x):
    """Creates a typed literal value containing the value |x|.

    Args:
      x: A string, int, float or bool value.

    Returns:
      {mojom_types.LiteralValue} with an appropriately typed value.
    """
    if isinstance(x, str):
      return mojom_types_mojom.LiteralValue(string_value=x)
    elif isinstance(x, int):
      return mojom_types_mojom.LiteralValue(int64_value=x)
    elif isinstance(x, float):
      return mojom_types_mojom.LiteralValue(double_value=x)
    elif isinstance(x, bool):
      return mojom_types_mojom.LiteralValue(bool_value=x)
    raise Exception("unexpected type(x)=%s" % type(x))


  def test_attributes(self):
    mojom_enum = mojom_types_mojom.MojomEnum()
    mojom_enum.decl_data = mojom_types_mojom.DeclarationData()
    gold = {
        'foo': 'bar',
        'other': 'thing',
        'hello': 'world',
        'min_version': 2,
        'pi': 3.14159,
        'is_happy': True
        }
    mojom_enum.decl_data.attributes = []
    for key, value in gold.iteritems():
      mojom_enum.decl_data.attributes.append(
          mojom_types_mojom.Attribute(key=key, value=self.literal_value(value)))

    graph = mojom_files_mojom.MojomFileGraph()
    attributes = mojom_translator.FileTranslator(
        graph, None).AttributesFromMojom(mojom_enum)

    self.assertEquals(gold, attributes)

  def test_attributes_none(self):
    mojom_enum = mojom_types_mojom.MojomEnum()
    mojom_enum.decl_data = mojom_types_mojom.DeclarationData()
    graph = mojom_files_mojom.MojomFileGraph()
    attributes = mojom_translator.FileTranslator(
        graph, None).AttributesFromMojom(mojom_enum)
    self.assertFalse(attributes)

  def test_imported_struct(self):
    graph = mojom_files_mojom.MojomFileGraph()

    graph.files = {
        'a.mojom': mojom_files_mojom.MojomFile(
            file_name='a.mojom',
            specified_file_name='',
            module_namespace='namespace',
            imports=['root/c.mojom']),
        'root/c.mojom': mojom_files_mojom.MojomFile(
            file_name='root/c.mojom',
            specified_file_name='',
            module_namespace='otherns',
            imports=[]),
    }

    mojom_struct = mojom_types_mojom.MojomStruct()
    mojom_struct.decl_data = mojom_types_mojom.DeclarationData(
        short_name='AStruct',
        source_file_info=mojom_types_mojom.SourceFileInfo(
          file_name='root/c.mojom'))
    mojom_struct.fields = []
    add_version_info(mojom_struct, 0)

    type_key = 'some_type_key'
    graph.resolved_types = {
        type_key: mojom_types_mojom.UserDefinedType(struct_type=mojom_struct)}
    struct = module.Struct()

    # Translate should create the imports.
    translator = mojom_translator.FileTranslator(graph, 'a.mojom')
    translator.Translate()

    struct = translator.UserDefinedFromTypeRef(
        mojom_types_mojom.Type(
          type_reference=mojom_types_mojom.TypeReference(
            type_key=type_key)))

    self.assertEquals(
        translator._transitive_imports['root/c.mojom']['module'], struct.module)
    self.assertEquals(
        translator._transitive_imports['root/c.mojom'], struct.imported_from)

  def test_interface(self):
    self.do_interface_test(True)
    self.do_interface_test(False)

  def do_interface_test(self, specify_service_name):
    file_name = 'a.mojom'
    mojom_interface = mojom_types_mojom.MojomInterface(
        current_version=47,
        decl_data=mojom_types_mojom.DeclarationData(
          short_name='AnInterface',
          source_file_info=mojom_types_mojom.SourceFileInfo(
            file_name=file_name)))
    if specify_service_name:
      mojom_interface.service_name = 'test::TheInterface'
      mojom_interface.decl_data.attributes = [mojom_types_mojom.Attribute(
          key='ServiceName', value=mojom_types_mojom.LiteralValue(
              string_value='test::TheInterface'))]
    else:
      mojom_interface.service_name = None
    mojom_method10 = mojom_types_mojom.MojomMethod(
        ordinal=10,
        decl_data=mojom_types_mojom.DeclarationData(
          short_name='AMethod10',
          declaration_order=1,
          source_file_info=mojom_types_mojom.SourceFileInfo(
            file_name=file_name)),
        parameters=mojom_types_mojom.MojomStruct(fields=[],
            version_info=build_version_info(0),
            decl_data=build_decl_data('AMethod10_Request')))
    mojom_method0 = mojom_types_mojom.MojomMethod(
        ordinal=0,
        decl_data=mojom_types_mojom.DeclarationData(
          short_name='AMethod0',
           declaration_order=1,
          source_file_info=mojom_types_mojom.SourceFileInfo(
            file_name=file_name)),
        parameters=mojom_types_mojom.MojomStruct(fields=[],
            version_info=build_version_info(0),
            decl_data=build_decl_data('AMethod0_Request')))
    mojom_method7 = mojom_types_mojom.MojomMethod(
        ordinal=7,
        decl_data=mojom_types_mojom.DeclarationData(
          short_name='AMethod7',
          declaration_order=0,
          source_file_info=mojom_types_mojom.SourceFileInfo(
            file_name=file_name)),
        parameters=mojom_types_mojom.MojomStruct(fields=[],
            version_info=build_version_info(0),
            decl_data=build_decl_data('AMethod7_Request')))
    mojom_interface.methods = {10: mojom_method10, 0: mojom_method0,
        7: mojom_method7}

    interface = module.Interface()
    graph = mojom_files_mojom.MojomFileGraph()
    translator = mojom_translator.FileTranslator(graph, file_name)
    translator.InterfaceFromMojom(interface, mojom_types_mojom.UserDefinedType(
      interface_type=mojom_interface))


    self.assertEquals(translator._module, interface.module)
    self.assertEquals('AnInterface', interface.name)
    self.assertEquals(mojom_interface.current_version, interface.version)
    # The methods should be ordered by declaration_order.
    self.assertEquals(7, interface.methods[0].ordinal)
    self.assertEquals(0, interface.methods[1].ordinal)
    self.assertEquals(10, interface.methods[2].ordinal)
    if specify_service_name:
      self.assertEquals('test::TheInterface', interface.service_name)
    else:
      self.assertEquals(None, interface.service_name)

    # TODO(azani): Add the contained declarations.

  def test_method(self):
    file_name = 'a.mojom'
    mojom_method = mojom_types_mojom.MojomMethod(
        ordinal=10,
        min_version=6,
        decl_data=mojom_types_mojom.DeclarationData(
          short_name='AMethod',
          source_file_info=mojom_types_mojom.SourceFileInfo(
            file_name=file_name)))

    param1 = mojom_types_mojom.StructField(
        decl_data=mojom_types_mojom.DeclarationData(short_name='a_param'),
        type=mojom_types_mojom.Type(
          simple_type=mojom_types_mojom.SimpleType.UINT32),
        offset=21,
        bit=6,
        min_version=11)
    param2 = mojom_types_mojom.StructField(
        decl_data=mojom_types_mojom.DeclarationData(short_name='b_param'),
        type=mojom_types_mojom.Type(
          simple_type=mojom_types_mojom.SimpleType.UINT64),
        offset=22,
        bit=7,
        min_version=12)
    mojom_method.parameters = mojom_types_mojom.MojomStruct(
        fields=[param1, param2],
        version_info=build_version_info(2),
        decl_data=build_decl_data('Not used'))

    interface = module.Interface('MyInterface')
    graph = mojom_files_mojom.MojomFileGraph()
    translator = mojom_translator.FileTranslator(graph, file_name)
    method = translator.MethodFromMojom(mojom_method, interface)

    self.assertEquals(mojom_method.decl_data.short_name, method.name)
    self.assertEquals(interface, method.interface)
    self.assertEquals(mojom_method.ordinal, method.ordinal)
    self.assertEquals(mojom_method.min_version, method.min_version)
    self.assertIsNone(method.response_parameters)
    self.assertEquals(
        len(mojom_method.parameters.fields), len(method.parameters))
    self.assertEquals(param1.decl_data.short_name, method.parameters[0].name)
    self.assertEquals(param2.decl_data.short_name, method.parameters[1].name)
    self.assertEquals('MyInterface_AMethod_Params', method.param_struct.name)
    self.assertEquals(len(mojom_method.parameters.fields),
      len(method.param_struct.fields))
    for i in xrange(0, len(mojom_method.parameters.fields)):
        gold = mojom_method.parameters.fields[i]
        f = method.param_struct.fields_in_ordinal_order[i]
        self.assertEquals(gold.decl_data.short_name, f.name)
        self.assertEquals(gold.offset, f.computed_offset)
        self.assertEquals(gold.bit, f.computed_bit)
        self.assertEquals(gold.min_version, f.computed_min_version)

    # Add empty return params.
    mojom_method.response_params = mojom_types_mojom.MojomStruct(fields=[])
    add_version_info(mojom_method.response_params, 0)
    add_decl_data(mojom_method.response_params, 'AMethod_Response')
    method = translator.MethodFromMojom(mojom_method, interface)
    self.assertEquals([], method.response_parameters)

    # Add non-empty return params.
    mojom_method.response_params.fields = [param1]
    method = translator.MethodFromMojom(mojom_method, interface)
    self.assertEquals(
        param1.decl_data.short_name, method.response_parameters[0].name)

  def test_parameter(self):
    # Parameters are encoded as fields in a struct.
    mojom_param = mojom_types_mojom.StructField(
        decl_data=mojom_types_mojom.DeclarationData(
          short_name='param0',
          declared_ordinal=5),
        type=mojom_types_mojom.Type(
          simple_type=mojom_types_mojom.SimpleType.UINT64),
        default_value=mojom_types_mojom.Value(
          literal_value=mojom_types_mojom.LiteralValue(uint64_value=20)))

    graph = mojom_files_mojom.MojomFileGraph()
    translator = mojom_translator.FileTranslator(graph, '')
    param = translator.ParamFromMojom(mojom_param)

    self.assertEquals(mojom_param.decl_data.short_name, param.name)
    self.assertEquals(module.UINT64, param.kind)
    self.assertEquals(mojom_param.decl_data.declared_ordinal, param.ordinal)

  def test_contained_declarations(self):
    graph = mojom_files_mojom.MojomFileGraph()
    file_name = 'root/f.mojom'

    mojom_enum = mojom_types_mojom.MojomEnum(
        values=[],
        decl_data=mojom_types_mojom.DeclarationData(
          short_name='AnEnum',
          source_file_info=mojom_types_mojom.SourceFileInfo(
            file_name=file_name),
          container_type_key='parent_key'))
    graph.resolved_types = {
        'enum_key': mojom_types_mojom.UserDefinedType(enum_type=mojom_enum)}

    mojom_const = mojom_types_mojom.DeclaredConstant(
        decl_data=mojom_types_mojom.DeclarationData(
          short_name='AConst',
          container_type_key='parent_key'),
        type=mojom_types_mojom.Type(
          simple_type=mojom_types_mojom.SimpleType.INT64),
        value=mojom_types_mojom.Value(
          literal_value=mojom_types_mojom.LiteralValue(
            int64_value=30)))
    graph.resolved_constants = {'constant_key': mojom_const}

    contained_declarations = mojom_types_mojom.ContainedDeclarations(
        enums=['enum_key'], constants=['constant_key'])

    translator = mojom_translator.FileTranslator(graph, file_name)
    struct = module.Struct(name='parent')
    translator._type_cache['parent_key'] = struct
    translator.PopulateContainedDeclarationsFromMojom(
        struct, contained_declarations)

    self.assertEquals(
        mojom_enum.decl_data.short_name, struct.enums[0].name)
    self.assertEquals(struct, struct.enums[0].parent_kind)
    self.assertEquals(
        mojom_const.decl_data.short_name, struct.constants[0].name)
    self.assertEquals(struct, struct.constants[0].parent_kind)


@unittest.skipUnless(bindings_imported, 'Could not import python bindings.')
class TestValueFromMojom(unittest.TestCase):

  def test_literal_value(self):
    mojom_int64 = mojom_types_mojom.Value()
    mojom_int64.literal_value = mojom_types_mojom.LiteralValue(int64_value=20)
    mojom_bool = mojom_types_mojom.Value()
    mojom_bool.literal_value = mojom_types_mojom.LiteralValue(bool_value=True)
    mojom_double = mojom_types_mojom.Value()
    mojom_double.literal_value = mojom_types_mojom.LiteralValue(
        double_value=1234.012345678901)

    graph = mojom_files_mojom.MojomFileGraph()
    int64_const = mojom_translator.FileTranslator(graph, None).ValueFromMojom(
        mojom_int64)
    bool_const = mojom_translator.FileTranslator(graph, None).ValueFromMojom(
        mojom_bool)
    double_const = mojom_translator.FileTranslator(graph, None).ValueFromMojom(
        mojom_double)

    self.assertEquals('20', int64_const)
    self.assertEquals('true', bool_const)
    self.assertEquals('1234.012345678901', double_const)

  def test_builtin_const(self):
    mojom = mojom_types_mojom.Value()

    graph = mojom_files_mojom.MojomFileGraph()

    gold = [
        (mojom_types_mojom.BuiltinConstantValue.DOUBLE_INFINITY,
          'double.INFINITY'),
        (mojom_types_mojom.BuiltinConstantValue.DOUBLE_NEGATIVE_INFINITY,
          'double.NEGATIVE_INFINITY'),
        (mojom_types_mojom.BuiltinConstantValue.DOUBLE_NAN,
          'double.NAN'),
        (mojom_types_mojom.BuiltinConstantValue.FLOAT_INFINITY,
          'float.INFINITY'),
        (mojom_types_mojom.BuiltinConstantValue.FLOAT_NEGATIVE_INFINITY,
          'float.NEGATIVE_INFINITY'),
        (mojom_types_mojom.BuiltinConstantValue.FLOAT_NAN, 'float.NAN'),
        ]

    for mojom_builtin, string in gold:
      mojom.builtin_value = mojom_builtin
      const = mojom_translator.FileTranslator(graph, None).ValueFromMojom(mojom)
      self.assertIsInstance(const, module.BuiltinValue)
      self.assertEquals(string, const.value)

  def test_enum_value(self):
    file_name = 'a.mojom'
    mojom_enum = mojom_types_mojom.MojomEnum()
    mojom_enum.decl_data = mojom_types_mojom.DeclarationData(
        short_name='AnEnum',
        source_file_info=mojom_types_mojom.SourceFileInfo(file_name=file_name))
    value1 = mojom_types_mojom.EnumValue(
        decl_data=mojom_types_mojom.DeclarationData(
          short_name='val1',
          source_file_info=mojom_types_mojom.SourceFileInfo(
            file_name=file_name)),
        initializer_value=mojom_types_mojom.Value(
            literal_value=mojom_types_mojom.LiteralValue(uint64_value=20)),
        int_value=20)
    value2 = mojom_types_mojom.EnumValue(
        decl_data=mojom_types_mojom.DeclarationData(short_name='val2'),
        int_value=70)
    mojom_enum.values = [value1, value2]

    graph = mojom_files_mojom.MojomFileGraph()
    graph.resolved_types = {
        'enum_key': mojom_types_mojom.UserDefinedType(enum_type=mojom_enum)}

    mojom = mojom_types_mojom.Value(
        enum_value_reference=mojom_types_mojom.EnumValueReference(
          identifier='SOMEID',
          enum_type_key='enum_key',
          enum_value_index=0))

    translator = mojom_translator.FileTranslator(graph, file_name)
    enum_value = translator.ValueFromMojom(mojom)
    enum = translator.UserDefinedFromTypeKey('enum_key')

    self.assertIs(enum, enum_value.enum)
    self.assertIs(value1.decl_data.short_name, enum_value.name)

  def test_constant_value(self):
    file_name = 'a.mojom'
    mojom_const = mojom_types_mojom.DeclaredConstant(
        decl_data=mojom_types_mojom.DeclarationData(
          short_name='AConst',
          source_file_info=mojom_types_mojom.SourceFileInfo(
            file_name=file_name)),
        type=mojom_types_mojom.Type(
          simple_type=mojom_types_mojom.SimpleType.INT64),
        value=mojom_types_mojom.Value(
          literal_value=mojom_types_mojom.LiteralValue(
            int64_value=30)))

    graph = mojom_files_mojom.MojomFileGraph()
    graph.resolved_constants = {'constant_key': mojom_const}

    mojom = mojom_types_mojom.Value(
        constant_reference=mojom_types_mojom.ConstantReference(
          identifier='SOMEID',
          constant_key='constant_key'))

    translator = mojom_translator.FileTranslator(graph, file_name)
    const_value = translator.ValueFromMojom(mojom)
    self.assertIs(
        translator.ConstantFromKey('constant_key'), const_value.constant)
    self.assertIs(mojom_const.decl_data.short_name, const_value.name)


@unittest.skipUnless(bindings_imported, 'Could not import python bindings.')
class TestKindFromMojom(unittest.TestCase):

  def test_simple_type(self):
    simple_types = [
        (mojom_types_mojom.SimpleType.BOOL, module.BOOL),
        (mojom_types_mojom.SimpleType.INT8, module.INT8),
        (mojom_types_mojom.SimpleType.INT16, module.INT16),
        (mojom_types_mojom.SimpleType.INT32, module.INT32),
        (mojom_types_mojom.SimpleType.INT64, module.INT64),
        (mojom_types_mojom.SimpleType.UINT8, module.UINT8),
        (mojom_types_mojom.SimpleType.UINT16, module.UINT16),
        (mojom_types_mojom.SimpleType.UINT32, module.UINT32),
        (mojom_types_mojom.SimpleType.UINT64, module.UINT64),
        (mojom_types_mojom.SimpleType.FLOAT, module.FLOAT),
        (mojom_types_mojom.SimpleType.DOUBLE, module.DOUBLE),
    ]

    g = mojom_files_mojom.MojomFileGraph()
    t = mojom_translator.FileTranslator(g, None)
    for mojom, golden in simple_types:
      self.assertEquals(
          golden, t.KindFromMojom(mojom_types_mojom.Type(simple_type=mojom)))

  def test_handle_type(self):
    handle_types = [
        (mojom_types_mojom.HandleType.Kind.UNSPECIFIED, False,
          module.HANDLE),
        (mojom_types_mojom.HandleType.Kind.MESSAGE_PIPE, False,
          module.MSGPIPE),
        (mojom_types_mojom.HandleType.Kind.DATA_PIPE_CONSUMER, False,
          module.DCPIPE),
        (mojom_types_mojom.HandleType.Kind.DATA_PIPE_PRODUCER, False,
          module.DPPIPE),
        (mojom_types_mojom.HandleType.Kind.SHARED_BUFFER, False,
          module.SHAREDBUFFER),
        (mojom_types_mojom.HandleType.Kind.UNSPECIFIED, True,
          module.NULLABLE_HANDLE),
        (mojom_types_mojom.HandleType.Kind.MESSAGE_PIPE, True,
          module.NULLABLE_MSGPIPE),
        (mojom_types_mojom.HandleType.Kind.DATA_PIPE_CONSUMER, True,
          module.NULLABLE_DCPIPE),
        (mojom_types_mojom.HandleType.Kind.DATA_PIPE_PRODUCER, True,
          module.NULLABLE_DPPIPE),
        (mojom_types_mojom.HandleType.Kind.SHARED_BUFFER, True,
          module.NULLABLE_SHAREDBUFFER),
    ]
    g = mojom_files_mojom.MojomFileGraph()
    t = mojom_translator.FileTranslator(g, None)
    for mojom, nullable, golden in handle_types:
      h = mojom_types_mojom.Type()
      h.handle_type = mojom_types_mojom.HandleType(
          kind=mojom, nullable=nullable)
      self.assertEquals(golden, t.KindFromMojom(h))

  def test_string_type(self):
    g = mojom_files_mojom.MojomFileGraph()
    t = mojom_translator.FileTranslator(g, None)

    s = mojom_types_mojom.Type(string_type=mojom_types_mojom.StringType())
    self.assertEquals(module.STRING, t.KindFromMojom(s))

    s.string_type.nullable = True
    self.assertEquals(module.NULLABLE_STRING, t.KindFromMojom(s))

  def test_array_type(self):
    array_types = [
        (False, False, -1),
        (False, False, 10),
        (True, False, -1),
        (True, True, -1),
        (False, True, -1),
        (False, True, 10),
        ]
    g = mojom_files_mojom.MojomFileGraph()
    t = mojom_translator.FileTranslator(g, None)

    for array_nullable, element_nullable, size in array_types:
      a = mojom_types_mojom.Type()
      a.array_type = mojom_types_mojom.ArrayType(
          nullable=array_nullable,
          fixed_length=size)
      a.array_type.element_type = mojom_types_mojom.Type(
          string_type=mojom_types_mojom.StringType(nullable=element_nullable))

      result = t.KindFromMojom(a)
      self.assertTrue(module.IsArrayKind(result))
      self.assertTrue(module.IsStringKind(result.kind))
      self.assertEquals(array_nullable, module.IsNullableKind(result))
      self.assertEquals(element_nullable, module.IsNullableKind(result.kind))

      if size < 0:
        self.assertIsNone(result.length)
      else:
        self.assertEquals(size, result.length)

  def test_map_type(self):
    map_types = [
        (False, False),
        (True, False),
        (False, True),
        (True, True),
    ]
    g = mojom_files_mojom.MojomFileGraph()
    t = mojom_translator.FileTranslator(g, None)

    for map_nullable, value_nullable in map_types:
      m = mojom_types_mojom.Type()
      m.map_type = mojom_types_mojom.MapType(
          nullable=map_nullable)
      m.map_type.key_type = mojom_types_mojom.Type(
          string_type=mojom_types_mojom.StringType())
      m.map_type.value_type = mojom_types_mojom.Type(
          handle_type=mojom_types_mojom.HandleType(
            kind=mojom_types_mojom.HandleType.Kind.SHARED_BUFFER,
            nullable=value_nullable))

      result = t.KindFromMojom(m)
      self.assertTrue(module.IsMapKind(result))
      self.assertTrue(module.IsStringKind(result.key_kind))
      self.assertTrue(module.IsSharedBufferKind(result.value_kind))
      self.assertEquals(map_nullable, module.IsNullableKind(result))
      self.assertEquals(value_nullable,
          module.IsNullableKind(result.value_kind))

  def test_user_defined_type_type(self):
    graph = mojom_files_mojom.MojomFileGraph()
    mojom_struct = mojom_types_mojom.MojomStruct(
        decl_data=mojom_types_mojom.DeclarationData(short_name='FirstStruct'))
    type_key = 'some opaque string'
    mojom_struct.fields = [
        # Make sure recursive structs are correctly handled.
        mojom_types_mojom.StructField(
          decl_data=mojom_types_mojom.DeclarationData(short_name='field00'),
          type=mojom_types_mojom.Type(
            type_reference=mojom_types_mojom.TypeReference(type_key=type_key)))
        ]
    add_version_info(mojom_struct, 1)
    graph.resolved_types = {
        type_key: mojom_types_mojom.UserDefinedType(struct_type=mojom_struct)}

    mojom_type = mojom_types_mojom.Type()
    mojom_type.type_reference = mojom_types_mojom.TypeReference(
        type_key=type_key)

    t = mojom_translator.FileTranslator(graph, None)
    result = t.KindFromMojom(mojom_type)
    self.assertTrue(module.IsStructKind(result))
    self.assertEquals(mojom_struct.decl_data.short_name, result.name)
    self.assertEquals(result, result.fields[0].kind)
    self.assertEquals(type_key, result.type_key)

    # Make sure we create only one module object per type.
    result2 = t.KindFromMojom(mojom_type)
    self.assertIs(result, result2)

    # Nullable type reference
    mojom_type.type_reference.nullable = True
    nullable_result = t.KindFromMojom(mojom_type)
    self.assertTrue(module.IsNullableKind(nullable_result))

if __name__ == '__main__':
  unittest.main()

def build_decl_data(short_name):
  """Builds and returns a DeclarationData with the given short_name.

  Args:
    short_name: {str} short_name to use

  Returns:
    {mojom_types_mojom.DeclarationData} With the given short_name
  """
  return mojom_types_mojom.DeclarationData(short_name=short_name)

def add_decl_data(element, short_name):
  """Builds a DeclarationData with the given short_name and adds it
  as the |decl_data| attribute of |element|.

  Args:
    element: {any} The Python object to which a |decl_data| attribute will be
        added.
    short_name: {str} short_name to use
  """
  element.decl_data=build_decl_data(short_name)

def build_version_info(num_fields):
  """Builds and returns a list containing a single StructVersion with
  version_number=0, num_bytes=0, and the given value for num_fields.

  Args:
    num_fields: {int} The value of num_fields to use.
  Returns:
    {[mojom_types_mojom.StructVersion]} Containing a single element with
        the given value for num_fields.
  """
  return [mojom_types_mojom.StructVersion(
        version_number=0, num_bytes=0,num_fields=num_fields)]

def add_version_info(mojom_struct, num_fields):
  """Builds a list containing a single StructVersion with
  version_number=0, num_bytes=0, and the given value for num_fields. Adds this
  as the |version_info| attribute of |mojom_struct|.

  Args:
    mojom_struct: {any} The Python object to which a |version_info| attribute
        will be added.
    num_fields: {int} The value of num_fields to use.
  """
  mojom_struct.version_info=build_version_info(num_fields)
