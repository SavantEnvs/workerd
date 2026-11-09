// Stand-in for the V8 ArrayBuffer behind a jsg::JsBufferSource (see v8_stubs.c++). A fake
// v8::Local<v8::Value> is simply the address of one of these.
#pragma once

#include <cstddef>
#include <cstdint>

namespace mayhem {

struct FakeBuffer {
  uint8_t* data;
  size_t size;
};

}  // namespace mayhem
