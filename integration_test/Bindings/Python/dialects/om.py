# REQUIRES: bindings_python
# RUN: %PYTHON% %s | FileCheck %s

import circt
from circt.dialects import om
from circt.ir import Context, InsertionPoint, Location, Module
from circt.support import var_to_attribute

from dataclasses import dataclass

with Context() as ctx, Location.unknown():
  circt.register_dialects(ctx)

  module = Module.parse("""
  module {
    %sym = om.constant #om.ref<<@Root::@x>> : !om.ref

    om.class @Test(%param: !om.integer) {
      om.class.field @field, %param : !om.integer

      %c_14 = om.constant #om.integer<14> : !om.integer
      %0 = om.object @Child(%c_14) : (!om.integer) -> !om.class.type<@Child>
      om.class.field @child, %0 : !om.class.type<@Child>

      om.class.field @reference, %sym : !om.ref

      %list = om.constant #om.list<!om.string, ["X" : !om.string, "Y" : !om.string]> : !om.list<!om.string>
      om.class.field @list, %list : !om.list<!om.string>

      %tuple = om.tuple_create %list, %c_14: !om.list<!om.string>, !om.integer
      om.class.field @tuple, %tuple : tuple<!om.list<!om.string>, !om.integer>

      %c_15 = om.constant #om.integer<15> : !om.integer
      %1 = om.object @Child(%c_15) : (!om.integer) -> !om.class.type<@Child>
      %list_child = om.list_create %0, %1: !om.class.type<@Child>
      %2 = om.object @Nest(%list_child) : (!om.list<!om.class.type<@Child>>) -> !om.class.type<@Nest>
      om.class.field @nest, %2 : !om.class.type<@Nest>

      %3 = om.constant #om.map<!om.integer, {a = #om.integer<42>, b = #om.integer<32>}> : !om.map<!om.string, !om.integer>
      om.class.field @map, %3 : !om.map<!om.string, !om.integer>

      %x = om.constant "X" : !om.string
      %y = om.constant "Y" : !om.string
      %entry1 = om.tuple_create %x, %c_14: !om.string, !om.integer
      %entry2 = om.tuple_create %y, %c_15: !om.string, !om.integer

      %map = om.map_create %entry1, %entry2: !om.string, !om.integer
      om.class.field @map_create, %map : !om.map<!om.string, !om.integer>
    }

    om.class @Child(%0: !om.integer) {
      om.class.field @foo, %0 : !om.integer
    }

    om.class @Nest(%0: !om.list<!om.class.type<@Child>>) {
      om.class.field @list_child, %0 : !om.list<!om.class.type<@Child>>
    }

    hw.module @Root(%clock: i1) -> () {
      %0 = sv.wire sym @x : !hw.inout<i1>
    }
  }
  """)

  evaluator = om.Evaluator(module)

# Test instantiate failure.

try:
  obj = evaluator.instantiate("Test")
except ValueError as e:
  # CHECK: actual parameter list length (0) does not match
  # CHECK: actual parameters:
  # CHECK: formal parameters:
  # CHECK: unable to instantiate object, see previous error(s)
  print(e)

# Test get field failure.

try:
  obj = evaluator.instantiate("Test", 42)
  obj.foo
except ValueError as e:
  # CHECK: field "foo" does not exist
  # CHECK: see current operation:
  # CHECK: unable to get field, see previous error(s)
  print(e)

# Test instantiate success.

obj = evaluator.instantiate("Test", 42)

assert isinstance(obj.type, om.ClassType)

# CHECK: Test
print(obj.type.name)

# CHECK: 42
print(obj.field)
# CHECK: 14
print(obj.child.foo)
# CHECK: ('Root', 'x')
print(obj.reference)
# CHECK: 14
(fst, snd) = obj.tuple
print(snd)

try:
  print(obj.tuple[3])
except IndexError as e:
  # CHECK: tuple index out of range
  print(e)

for (name, field) in obj:
  # CHECK: name: child, field: <circt.dialects.om.Object object
  # CHECK: name: field, field: 42
  # CHECK: name: reference, field: ('Root', 'x')
  print(f"name: {name}, field: {field}")

# CHECK: ['X', 'Y']
print(obj.list)
for child in obj.nest.list_child:
  # CHECK: 14
  # CHECK-NEXT: 15
  print(child.foo)

# CHECK: 2
print(len(obj.map))
# CHECK: {'a': 42, 'b': 32}
print(obj.map)
for k, v in obj.map.items():
  # CHECK-NEXT: a 42
  # CHECK-NEXT: b 32
  print(k, v)

try:
  print(obj.map_create[1])
except KeyError as e:
  # CHECK-NEXT: 'key is not integer'
  print(e)
try:
  print(obj.map_create["INVALID"])
except KeyError as e:
  # CHECK-NEXT: 'key not found'
  print(e)
# CHECK-NEXT: 14
print(obj.map_create["X"])

for k, v in obj.map_create.items():
  # CHECK-NEXT: X 14
  # CHECK-NEXT: Y 15
  print(k, v)
