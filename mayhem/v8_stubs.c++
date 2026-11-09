// Stand-ins for the V8 and jsg symbols that zlib-util.c++ and compression.c++ reference. The write path
// under test reaches only JsBufferSource::asArrayPtr() (the bytes behind a fake ArrayBuffer, v8_fake.h)
// and the external-memory accounting of CompressionAllocator, which are real stand-ins below; every other
// symbol exists to satisfy the linker (JS-facing code the harness never calls) and aborts if reached.
#include "v8_fake.h"

#include <workerd/io/features.h>
#include <workerd/jsg/jsg.h>
#include <workerd/jsg/util.h>
#include <workerd/jsg/wrappable.h>

#include <cppgc/persistent.h>
#include <v8-cppgc.h>
#include <v8-exception.h>
#include <v8-local-handle.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>

#define UNREACHABLE_STUB() \
  do { \
    fprintf(stderr, "v8_stubs: %s reached\n", __func__); \
    abort(); \
  } while (0)

namespace workerd::jsg {

kj::ArrayPtr<kj::byte> JsBufferSource::asArrayPtr() {
  v8::Local<v8::Value> inner = *this;
  mayhem::FakeBuffer* buf;
  memcpy(&buf, &inner, sizeof(buf));
  return kj::ArrayPtr<kj::byte>(buf->data, buf->size);
}

JsValue::JsValue(v8::Local<v8::Value> inner): inner(inner) {}

ExternalMemoryAdjustment::ExternalMemoryAdjustment(
    kj::Arc<const ExternalMemoryTarget> externalMemory, size_t amount)
    : externalMemory(kj::mv(externalMemory)),
      amount(amount) {}

ExternalMemoryAdjustment::ExternalMemoryAdjustment(ExternalMemoryAdjustment&& other) noexcept
    : externalMemory(kj::mv(other.externalMemory)),
      amount(other.amount) {
  other.amount = 0;
}

ExternalMemoryAdjustment& ExternalMemoryAdjustment::operator=(ExternalMemoryAdjustment&& other) {
  externalMemory = kj::mv(other.externalMemory);
  amount = other.amount;
  other.amount = 0;
  return *this;
}

ExternalMemoryAdjustment::~ExternalMemoryAdjustment() noexcept(false) {}

ExternalMemoryAdjustment ExternalMemoryTarget::getAdjustment(size_t amount) const {
  return ExternalMemoryAdjustment(this->addRefToThis(), amount);
}

const char* JsExceptionThrown::what() const noexcept {
  UNREACHABLE_STUB();
}

void Wrappable::jsgVisitForGc(GcVisitor&) {
  UNREACHABLE_STUB();
}
void Wrappable::jsgGetMemoryInfo(MemoryTracker&) const {
  UNREACHABLE_STUB();
}
void Wrappable::addStrongRef() {
  UNREACHABLE_STUB();
}
kj::Maybe<v8::Local<v8::Object>> Wrappable::tryGetHandle(v8::Isolate*) {
  UNREACHABLE_STUB();
}
void Wrappable::maybeDeferDestruction(bool, kj::Own<void>, Wrappable*) {
  UNREACHABLE_STUB();
}
void Wrappable::visitRef(GcVisitor&, kj::Maybe<Wrappable&>&, bool&) {
  UNREACHABLE_STUB();
}

void Data::destroy() {
  UNREACHABLE_STUB();
}
kj::Arc<const IsolateLiveness> Data::getIsolateLiveness(v8::Isolate*) {
  UNREACHABLE_STUB();
}
void Data::moveFromTraced(Data&, v8::TracedReference<v8::Data>&) noexcept {
  UNREACHABLE_STUB();
}
void GcVisitor::visit(Data&) {
  UNREACHABLE_STUB();
}

kj::String JsString::toString(Lock&) const {
  UNREACHABLE_STUB();
}
Value Lock::exceptionToJs(kj::Exception&&, ExceptionToJsOptions) {
  UNREACHABLE_STUB();
}
kj::Arc<const ExternalMemoryTarget> Lock::getExternalMemoryTarget() {
  UNREACHABLE_STUB();
}
#ifdef WORKERD_ASAN
void Lock::maybeAllocGcStress() {
  UNREACHABLE_STUB();
}
#endif
void throwInternalError(v8::Isolate*, kj::Exception&&, ExceptionToJsOptions) {
  UNREACHABLE_STUB();
}
void throwInternalError(v8::Isolate*, kj::StringPtr) {
  UNREACHABLE_STUB();
}
kj::Maybe<const void*> tryGetTypeHandlerErased(v8::Isolate*, const std::type_info&) {
  UNREACHABLE_STUB();
}

}  // namespace workerd::jsg

namespace workerd {
CompatibilityFlags::Reader FeatureFlags::get(jsg::Lock&) {
  UNREACHABLE_STUB();
}
}  // namespace workerd

namespace cppgc::internal {
PersistentRegion& WeakPersistentPolicy::GetPersistentRegion(const void*) {
  UNREACHABLE_STUB();
}
}  // namespace cppgc::internal

namespace v8 {

bool Value::IsArrayBufferView() const {
  UNREACHABLE_STUB();
}
size_t ArrayBufferBase::ByteLength() const {
  UNREACHABLE_STUB();
}
void* ArrayBufferBase::Data() const {
  UNREACHABLE_STUB();
}
bool ArrayBufferBase::IsImmutable() const {
  UNREACHABLE_STUB();
}
bool ArrayBufferBase::WasDetached() const {
  UNREACHABLE_STUB();
}
Local<ArrayBuffer> ArrayBufferView::Buffer() {
  UNREACHABLE_STUB();
}
size_t ArrayBufferView::ByteLength() {
  UNREACHABLE_STUB();
}
size_t ArrayBufferView::ByteOffset() {
  UNREACHABLE_STUB();
}
internal::Address* HandleScope::Extend(Isolate*) {
  UNREACHABLE_STUB();
}
bool TryCatch::CanContinue() const {
  UNREACHABLE_STUB();
}
Local<Value> TryCatch::Exception() const {
  UNREACHABLE_STUB();
}
bool TryCatch::HasCaught() const {
  UNREACHABLE_STUB();
}
Local<Value> TryCatch::ReThrow() {
  UNREACHABLE_STUB();
}
TryCatch::TryCatch(Isolate*) {
  UNREACHABLE_STUB();
}
TryCatch::~TryCatch() {}

namespace api_internal {
void DisposeGlobal(internal::Address*) {
  UNREACHABLE_STUB();
}
internal::Address* GlobalizeReference(internal::Isolate*, internal::Address) {
  UNREACHABLE_STUB();
}
void MoveGlobalReference(internal::Address**, internal::Address**) {
  UNREACHABLE_STUB();
}
}  // namespace api_internal

}  // namespace v8
