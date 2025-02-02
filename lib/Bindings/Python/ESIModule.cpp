//===- ESIModule.cpp - ESI API pybind module ------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "DialectModules.h"

#include "circt-c/Dialect/ESI.h"
#include "mlir-c/Bindings/Python/Interop.h"

#include "mlir/Bindings/Python/PybindAdaptors.h"
#include "mlir/CAPI/IR.h"
#include "mlir/CAPI/Support.h"

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallVector.h"

#include "PybindUtils.h"
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
namespace py = pybind11;

//===----------------------------------------------------------------------===//
// The main entry point into the ESI Assembly API.
//===----------------------------------------------------------------------===//

// Mapping from unique identifier to python callback. We use std::string
// pointers since we also need to allocate memory for the string.
llvm::DenseMap<std::string *, PyObject *> serviceGenFuncLookup;
static MlirLogicalResult serviceGenFunc(MlirOperation reqOp,
                                        MlirOperation declOp, void *userData) {
  std::string *name = static_cast<std::string *>(userData);
  py::handle genFunc(serviceGenFuncLookup[name]);
  py::gil_scoped_acquire();
  py::object rc = genFunc(reqOp);
  return rc.cast<bool>() ? mlirLogicalResultSuccess()
                         : mlirLogicalResultFailure();
}

void registerServiceGenerator(std::string name, py::object genFunc) {
  std::string *n = new std::string(name);
  genFunc.inc_ref();
  serviceGenFuncLookup[n] = genFunc.ptr();
  circtESIRegisterGlobalServiceGenerator(wrap(*n), serviceGenFunc, n);
}

class PyAppIDIndex {
public:
  PyAppIDIndex(MlirOperation root) { index = circtESIAppIDIndexGet(root); }
  PyAppIDIndex(const PyAppIDIndex &) = delete;
  ~PyAppIDIndex() { circtESIAppIDIndexFree(index); }

  MlirAttribute getChildAppIDsOf(MlirOperation op) const {
    return circtESIAppIDIndexGetChildAppIDsOf(index, op);
  }

  py::object getAppIDPathAttr(MlirOperation fromMod, MlirAttribute appid,
                              MlirLocation loc) const {
    MlirAttribute path =
        circtESIAppIDIndexGetAppIDPath(index, fromMod, appid, loc);
    if (path.ptr == nullptr)
      return py::none();
    return py::cast(path);
  }

private:
  CirctESIAppIDIndex index;
};

using namespace mlir::python::adaptors;

void circt::python::populateDialectESISubmodule(py::module &m) {
  m.doc() = "ESI Python Native Extension";
  ::registerESIPasses();

  m.def("registerServiceGenerator", registerServiceGenerator,
        "Register a service generator for a given service name.",
        py::arg("impl_type"), py::arg("generator"));

  mlir_type_subclass(m, "ChannelType", circtESITypeIsAChannelType)
      .def_classmethod(
          "get",
          [](py::object cls, MlirType inner, uint32_t signaling = 0) {
            if (circtESITypeIsAChannelType(inner))
              return cls(inner);
            return cls(circtESIChannelTypeGet(inner, signaling));
          },
          py::arg("cls"), py::arg("inner"), py::arg("signaling") = 0)
      .def_property_readonly(
          "inner", [](MlirType self) { return circtESIChannelGetInner(self); })
      .def_property_readonly("signaling", [](MlirType self) {
        return circtESIChannelGetSignaling(self);
      });

  mlir_type_subclass(m, "AnyType", circtESITypeIsAnAnyType)
      .def_classmethod(
          "get",
          [](py::object cls, MlirContext ctxt) {
            return cls(circtESIAnyTypeGet(ctxt));
          },
          py::arg("self"), py::arg("ctxt") = nullptr);

  mlir_type_subclass(m, "ListType", circtESITypeIsAListType)
      .def_classmethod(
          "get",
          [](py::object cls, MlirType inner) {
            return cls(circtESIListTypeGet(inner));
          },
          py::arg("cls"), py::arg("inner"))
      .def_property_readonly("element_type", [](MlirType self) {
        return circtESIListTypeGetElementType(self);
      });

  mlir_attribute_subclass(m, "AppIDAttr", circtESIAttributeIsAnAppIDAttr)
      .def_classmethod(
          "get",
          [](py::object cls, std::string name, uint64_t index,
             MlirContext ctxt) {
            return cls(circtESIAppIDAttrGet(ctxt, wrap(name), index));
          },
          "Create an AppID attribute", py::arg("cls"), py::arg("name"),
          py::arg("index"), py::arg("context") = py::none())
      .def_property_readonly("name",
                             [](MlirAttribute self) {
                               llvm::StringRef name =
                                   unwrap(circtESIAppIDAttrGetName(self));
                               return std::string(name.data(), name.size());
                             })
      .def_property_readonly("index", [](MlirAttribute self) {
        return circtESIAppIDAttrGetIndex(self);
      });

  mlir_attribute_subclass(m, "AppIDPathAttr",
                          circtESIAttributeIsAnAppIDPathAttr)
      .def_classmethod(
          "get",
          [](py::object cls, MlirAttribute root,
             std::vector<MlirAttribute> path, MlirContext ctxt) {
            return cls(
                circtESIAppIDAttrPathGet(ctxt, root, path.size(), path.data()));
          },
          "Create an AppIDPath attribute", py::arg("cls"), py::arg("root"),
          py::arg("path"), py::arg("context") = py::none())
      .def_property_readonly("root", &circtESIAppIDAttrPathGetRoot)
      .def("__len__", &circtESIAppIDAttrPathGetNumComponents)
      .def("__getitem__", &circtESIAppIDAttrPathGetComponent);

  py::class_<PyAppIDIndex>(m, "AppIDIndex")
      .def(py::init<MlirOperation>(), py::arg("root"))
      .def("get_child_appids_of", &PyAppIDIndex::getChildAppIDsOf,
           "Return a dictionary of AppIDAttrs to ArrayAttr of InnerRefAttrs "
           "containing the relative paths to the leaf of the particular "
           "AppIDAttr. Argument MUST be HWModuleLike.",
           py::arg("mod"))
      .def("get_appid_path", &PyAppIDIndex::getAppIDPathAttr,
           "Return an array of InnerNameRefAttrs representing the relative "
           "path to 'appid' from 'fromMod'.",
           py::arg("from_mod"), py::arg("appid"),
           py::arg("query_site") = py::none());
}
