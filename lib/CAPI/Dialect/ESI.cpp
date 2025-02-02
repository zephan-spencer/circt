//===- ESI.cpp - C Interface for the ESI Dialect --------------------------===//
//
//===----------------------------------------------------------------------===//

#include "circt-c/Dialect/ESI.h"
#include "circt/Dialect/ESI/AppID.h"
#include "circt/Dialect/ESI/ESIServices.h"
#include "circt/Dialect/ESI/ESITypes.h"
#include "mlir/CAPI/IR.h"
#include "mlir/CAPI/Registration.h"
#include "mlir/CAPI/Support.h"
#include "mlir/CAPI/Utils.h"
#include "mlir/IR/Builders.h"
#include "mlir/IR/SymbolTable.h"
#include "mlir/Parser/Parser.h"
#include "mlir/Support/FileUtilities.h"

using namespace circt;
using namespace circt::esi;

MLIR_DEFINE_CAPI_DIALECT_REGISTRATION(ESI, esi, circt::esi::ESIDialect)

void registerESIPasses() { circt::esi::registerESIPasses(); }

MlirLogicalResult circtESIExportCosimSchema(MlirModule module,
                                            MlirStringCallback callback,
                                            void *userData) {
  mlir::detail::CallbackOstream stream(callback, userData);
  return wrap(circt::esi::exportCosimSchema(unwrap(module), stream));
}

bool circtESITypeIsAChannelType(MlirType type) {
  return unwrap(type).isa<ChannelType>();
}

MlirType circtESIChannelTypeGet(MlirType inner, uint32_t signaling) {
  auto signalEnum = symbolizeChannelSignaling(signaling);
  if (!signalEnum)
    return {};
  auto cppInner = unwrap(inner);
  return wrap(ChannelType::get(cppInner.getContext(), cppInner, *signalEnum));
}

MlirType circtESIChannelGetInner(MlirType channelType) {
  return wrap(unwrap(channelType).cast<ChannelType>().getInner());
}
uint32_t circtESIChannelGetSignaling(MlirType channelType) {
  return (uint32_t)unwrap(channelType).cast<ChannelType>().getSignaling();
}

bool circtESITypeIsAnAnyType(MlirType type) {
  return unwrap(type).isa<AnyType>();
}
MlirType circtESIAnyTypeGet(MlirContext ctxt) {
  return wrap(AnyType::get(unwrap(ctxt)));
}

bool circtESITypeIsAListType(MlirType type) {
  return unwrap(type).isa<ListType>();
}

MlirType circtESIListTypeGet(MlirType inner) {
  auto cppInner = unwrap(inner);
  return wrap(ListType::get(cppInner.getContext(), cppInner));
}

MlirType circtESIListTypeGetElementType(MlirType list) {
  return wrap(unwrap(list).cast<ListType>().getElementType());
}

void circtESIAppendMlirFile(MlirModule cMod, MlirStringRef filename) {
  ModuleOp modOp = unwrap(cMod);
  auto loadedMod =
      mlir::parseSourceFile<ModuleOp>(unwrap(filename), modOp.getContext());
  Block *loadedBlock = loadedMod->getBody();
  assert(!modOp->getRegions().empty());
  if (modOp.getBodyRegion().empty()) {
    modOp.getBodyRegion().push_back(loadedBlock);
    return;
  }
  auto &ops = modOp.getBody()->getOperations();
  ops.splice(ops.end(), loadedBlock->getOperations());
}
MlirOperation circtESILookup(MlirModule mod, MlirStringRef symbol) {
  return wrap(SymbolTable::lookupSymbolIn(unwrap(mod), unwrap(symbol)));
}

void circtESIRegisterGlobalServiceGenerator(
    MlirStringRef impl_type, CirctESIServiceGeneratorFunc genFunc,
    void *userData) {
  ServiceGeneratorDispatcher::globalDispatcher().registerGenerator(
      unwrap(impl_type), [genFunc, userData](ServiceImplementReqOp req,
                                             ServiceDeclOpInterface decl) {
        return unwrap(genFunc(wrap(req), wrap(decl.getOperation()), userData));
      });
}

//===----------------------------------------------------------------------===//
// AppID
//===----------------------------------------------------------------------===//

bool circtESIAttributeIsAnAppIDAttr(MlirAttribute attr) {
  return unwrap(attr).isa<AppIDAttr>();
}

MlirAttribute circtESIAppIDAttrGet(MlirContext ctxt, MlirStringRef name,
                                   uint64_t index) {
  return wrap(AppIDAttr::get(
      unwrap(ctxt), StringAttr::get(unwrap(ctxt), unwrap(name)), index));
}
MlirStringRef circtESIAppIDAttrGetName(MlirAttribute attr) {
  return wrap(unwrap(attr).cast<AppIDAttr>().getName().getValue());
}
uint64_t circtESIAppIDAttrGetIndex(MlirAttribute attr) {
  return unwrap(attr).cast<AppIDAttr>().getIndex();
}

bool circtESIAttributeIsAnAppIDPathAttr(MlirAttribute attr) {
  return isa<AppIDPathAttr>(unwrap(attr));
}

MlirAttribute circtESIAppIDAttrPathGet(MlirContext ctxt, MlirAttribute root,
                                       intptr_t numElements,
                                       MlirAttribute const *cElements) {
  SmallVector<AppIDAttr, 8> elements;
  for (intptr_t i = 0; i < numElements; ++i)
    elements.push_back(cast<AppIDAttr>(unwrap(cElements[i])));
  return wrap(AppIDPathAttr::get(
      unwrap(ctxt), cast<FlatSymbolRefAttr>(unwrap(root)), elements));
}
MlirAttribute circtESIAppIDAttrPathGetRoot(MlirAttribute attr) {
  return wrap(cast<AppIDPathAttr>(unwrap(attr)).getRoot());
}
uint64_t circtESIAppIDAttrPathGetNumComponents(MlirAttribute attr) {
  return cast<AppIDPathAttr>(unwrap(attr)).getPath().size();
}
MlirAttribute circtESIAppIDAttrPathGetComponent(MlirAttribute attr,
                                                uint64_t index) {
  return wrap(cast<AppIDPathAttr>(unwrap(attr)).getPath()[index]);
}

DEFINE_C_API_PTR_METHODS(CirctESIAppIDIndex, circt::esi::AppIDIndex)

/// Create an index of appids through which to do appid lookups efficiently.
MLIR_CAPI_EXPORTED CirctESIAppIDIndex
circtESIAppIDIndexGet(MlirOperation root) {
  auto *idx = new AppIDIndex(unwrap(root));
  if (idx->isValid())
    return wrap(idx);
  return CirctESIAppIDIndex{nullptr};
}

/// Free an AppIDIndex.
MLIR_CAPI_EXPORTED void circtESIAppIDIndexFree(CirctESIAppIDIndex index) {
  delete unwrap(index);
}

MLIR_CAPI_EXPORTED MlirAttribute
circtESIAppIDIndexGetChildAppIDsOf(CirctESIAppIDIndex idx, MlirOperation op) {
  auto mod = cast<hw::HWModuleLike>(unwrap(op));
  return wrap(unwrap(idx)->getChildAppIDsOf(mod));
}

MLIR_CAPI_EXPORTED
MlirAttribute circtESIAppIDIndexGetAppIDPath(CirctESIAppIDIndex idx,
                                             MlirOperation fromMod,
                                             MlirAttribute appid,
                                             MlirLocation loc) {
  auto mod = cast<hw::HWModuleLike>(unwrap(fromMod));
  auto path = cast<AppIDAttr>(unwrap(appid));
  FailureOr<ArrayAttr> instPath =
      unwrap(idx)->getAppIDPathAttr(mod, path, unwrap(loc));
  if (failed(instPath))
    return MlirAttribute{nullptr};
  return wrap(*instPath);
}
