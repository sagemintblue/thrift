// Copyright (c) 2006- Facebook
// Distributed under the Thrift Software License
//
// See accompanying file LICENSE or visit the Thrift site at:
// http://developers.facebook.com/thrift/

#define __STDC_LIMIT_MACROS
#include <stdint.h>
#include "TDenseProtocol.h"
#include "TReflectionLocal.h"

// XXX for debugging (duh)
#define DEBUG_TDENSEPROTOCOL

// XXX for development.
#define TDENSE_PROTOCOL_MEASURE_VLI

// The XXX above does not apply to this.
#ifdef DEBUG_TDENSEPROTOCOL
#undef NDEBUG
#endif
#include <cassert>

using std::string;

#ifdef __GNUC__
#define UNLIKELY(val) (__builtin_expect((val), 0))
#else
#define UNLIKELY(val) (val)
#endif

namespace facebook { namespace thrift { namespace protocol {

// Top TypeSpec.  TypeSpec of the structure being encoded.
#define TTS  (ts_stack_.back())  // type = TypeSpec*
// InDeX.  Index into TTS of the current/next field to encode.
#define IDX (idx_stack_.back())  // type = int
// Field TypeSpec.  TypeSpec of the current/next field to encode.
#define FTS (TTS->tstruct.specs[IDX])  // type = TypeSpec*
// Field MeTa.  Metadata of the current/next field to encode.
#define FMT (TTS->tstruct.metas[IDX])  // type = FieldMeta
// SubType 1/2.  TypeSpec of the first/second subtype of this container.
#define ST1 (TTS->tcontainer.subtype1)
#define ST2 (TTS->tcontainer.subtype2)


inline void TDenseProtocol::checkTType(const TType ttype) {
  assert(!ts_stack_.empty());
  assert(TTS->ttype == ttype);
}

inline void TDenseProtocol::stateTransition() {
  TypeSpec* old_tts = ts_stack_.back();
  ts_stack_.pop_back();

  if (ts_stack_.empty()) {
    assert(old_tts = type_spec_);
    return;
  }

  switch (TTS->ttype) {

    case T_STRUCT:
      assert(old_tts == FTS);
      break;

    case T_LIST:
    case T_SET:
      assert(old_tts == ST1);
      ts_stack_.push_back(old_tts);
      break;

    case T_MAP:
      assert(old_tts == (mkv_stack_.back() ? ST1 : ST2));
      mkv_stack_.back() = !mkv_stack_.back();
      ts_stack_.push_back(mkv_stack_.back() ? ST1 : ST2);
      break;

    default:
      assert(!"Invalid TType in stateTransition.");
      break;

  }
}

uint32_t TDenseProtocol::writeMessageBegin(const std::string& name,
                                           const TMessageType messageType,
                                           const int32_t seqid) {
  throw TApplicationException("TDenseProtocol doesn't work with messages (yet).");

  int32_t version = (VERSION_2) | ((int32_t)messageType);
  uint32_t wsize = 0;
  wsize += subWriteI32(version);
  wsize += subWriteString(name);
  wsize += subWriteI32(seqid);
  return wsize;
}

uint32_t TDenseProtocol::writeMessageEnd() {
  return 0;
}

// Also implements readStructBegin.
uint32_t TDenseProtocol::writeStructBegin(const string& name) {
  if (ts_stack_.empty()) {
    if (type_spec_ == NULL) {
      throw TApplicationException("TDenseProtocol: No type specified.");
    } else {
      ts_stack_.push_back(type_spec_);
    }
  }

  idx_stack_.push_back(0);
  return 0;
}

uint32_t TDenseProtocol::writeStructEnd() {
  idx_stack_.pop_back();
  stateTransition();
  return 0;
}

uint32_t TDenseProtocol::writeFieldBegin(const string& name,
                                         const TType fieldType,
                                         const int16_t fieldId) {
  uint32_t xfer = 0;

  while (FMT.tag != fieldId) {
    // TODO(dreiss): Old meta here.
    assert(FTS->ttype != T_STOP);
    assert(FMT.is_optional);
    xfer += subWriteBool(false);
    IDX++;
  }

  // TODO(dreiss): give a better exception.
  assert(FTS->ttype == fieldType);

  if (FMT.is_optional) {
    subWriteBool(true);
    xfer += 1;
  }

  // OMG I'm so gross.  XXX
  if (FTS->ttype != T_STOP) {
    ts_stack_.push_back(FTS);
  }
  return xfer;
}

uint32_t TDenseProtocol::writeFieldEnd() {
  IDX++;
  return 0;
}

uint32_t TDenseProtocol::writeFieldStop() {
  return writeFieldBegin("", T_STOP, 0);
}

uint32_t TDenseProtocol::writeMapBegin(const TType keyType,
                                       const TType valType,
                                       const uint32_t size) {
  checkTType(T_MAP);

  assert(keyType == ST1->ttype);
  assert(valType == ST2->ttype);

  ts_stack_.push_back(ST1);
  mkv_stack_.push_back(true);

  return subWriteI32((int32_t)size);
}

uint32_t TDenseProtocol::writeMapEnd() {
  ts_stack_.pop_back();
  mkv_stack_.pop_back();
  stateTransition();
  return 0;
}

uint32_t TDenseProtocol::writeListBegin(const TType elemType,
                                        const uint32_t size) {
  checkTType(T_LIST);

  assert(elemType == ST1->ttype);
  ts_stack_.push_back(ST1);
  return subWriteI32((int32_t)size);
}

uint32_t TDenseProtocol::writeListEnd() {
  ts_stack_.pop_back();
  stateTransition();
  return 0;
}

uint32_t TDenseProtocol::writeSetBegin(const TType elemType,
                                       const uint32_t size) {
  checkTType(T_SET);

  assert(elemType == ST1->ttype);
  ts_stack_.push_back(ST1);
  return subWriteI32((int32_t)size);
}

uint32_t TDenseProtocol::writeSetEnd() {
  ts_stack_.pop_back();
  stateTransition();
  return 0;
}

uint32_t TDenseProtocol::writeBool(const bool value) {
  checkTType(T_BOOL);
  stateTransition();
  return TBinaryProtocol::writeBool(value);
}

uint32_t TDenseProtocol::writeByte(const int8_t byte) {
  checkTType(T_BYTE);
  stateTransition();
  return TBinaryProtocol::writeByte(byte);
}

uint32_t TDenseProtocol::writeI16(const int16_t i16) {
  checkTType(T_I16);
  stateTransition();

  uint32_t rv = vliWrite(i16);
#ifdef TDENSE_PROTOCOL_MEASURE_VLI
  vli_save_16 += 2 - rv;
  if (i16 < 0) {
    negs++;
  }
#endif

  return rv;
}

uint32_t TDenseProtocol::writeI32(const int32_t i32) {
  checkTType(T_I32);
  stateTransition();

  uint32_t rv = vliWrite(i32);
#ifdef TDENSE_PROTOCOL_MEASURE_VLI
  vli_save_32 += 4 - rv;
  if (i32 < 0) {
    negs++;
  }
#endif

  return rv;
}

uint32_t TDenseProtocol::writeI64(const int64_t i64) {
  checkTType(T_I64);
  stateTransition();

  uint32_t rv = vliWrite(i64);
#ifdef TDENSE_PROTOCOL_MEASURE_VLI
  vli_save_64 += 8 - rv;
  if (i64 < 0) {
    negs++;
  }
#endif

  return rv;
}

uint32_t TDenseProtocol::writeDouble(const double dub) {
  checkTType(T_DOUBLE);
  stateTransition();
  return TBinaryProtocol::writeDouble(dub);
}

uint32_t TDenseProtocol::writeString(const std::string& str) {
  checkTType(T_STRING);
  stateTransition();
  return subWriteString(str);
}

inline uint32_t TDenseProtocol::subWriteI32(const int32_t i32) {
  uint32_t rv = vliWrite(i32);
#ifdef TDENSE_PROTOCOL_MEASURE_VLI
  vli_save_sub += 4 - rv;
  if (i32 < 0) {
    negs++;
  }
#endif
  return rv;
}

uint32_t TDenseProtocol::subWriteString(const std::string& str) {
  uint32_t size = str.size();
  uint32_t xfer = subWriteI32((int32_t)size);
  if (size > 0) {
    trans_->write((uint8_t*)str.data(), size);
  }
  return xfer + size;
}

inline uint32_t TDenseProtocol::vliRead(uint64_t& vli) {
  uint32_t used = 0;
  uint64_t val = 0;
  uint8_t buf[10];  // 64 bits / (7 bits/byte) = 10 bytes.
  bool borrowed = trans_->borrow(buf, sizeof(buf));

  // Fast path.  TODO(dreiss): Make it faster.
  if (borrowed) {
    while (true) {
      uint8_t byte = buf[used];
      used++;
      val = (val << 7) | (byte & 0x7f);
      if (!(byte & 0x80)) {
        vli = val;
        trans_->consume(used);
        return used;
      }
      // Have to check for invalid data so we don't crash.
      if (UNLIKELY(used == sizeof(buf))) {
        throw TProtocolException(TProtocolException::INVALID_DATA, "Variable-length int over 10 bytes.");
      }
    }
  }

  // Slow path.
  else {
    while (true) {
      uint8_t byte;
      used += trans_->readAll(&byte, 1);
      val = (val << 7) | (byte & 0x7f);
      if (!(byte & 0x80)) {
        vli = val;
        return used;
      }
      // Might as well check for invalid data on the slow path too.
      if (UNLIKELY(used >= sizeof(buf))) {
        throw TProtocolException(TProtocolException::INVALID_DATA, "Variable-length int over 10 bytes.");
      }
    }
  }
}

inline uint32_t TDenseProtocol::vliWrite(uint64_t vli) {
  uint8_t buf[10];  // 64 bits / (7 bits/byte) = 10 bytes.
  int32_t pos = sizeof(buf) - 1;

  // Write the thing from back to front.
  buf[pos] = vli & 0x7f;
  vli >>= 7;
  pos--;

  while (vli > 0) {
    assert(pos >= 0);
    buf[pos] = (vli | 0x80);
    vli >>= 7;
    pos--;
  }

  // Back up one step before writing.
  pos++;

  trans_->write(buf+pos, sizeof(buf) - pos);
  return sizeof(buf) - pos;
}

/**
 * Reading functions
 */

uint32_t TDenseProtocol::readMessageBegin(std::string& name,
                                          TMessageType& messageType,
                                          int32_t& seqid) {
  throw TApplicationException("TDenseProtocol doesn't work with messages (yet).");

  uint32_t xfer = 0;
  int32_t sz;
  xfer += subReadI32(sz);

  if (sz < 0) {
    // Check for correct version number
    int32_t version = sz & VERSION_MASK;
    if (version != VERSION_2) {
      throw TProtocolException(TProtocolException::BAD_VERSION, "Bad version identifier");
    }
    messageType = (TMessageType)(sz & 0x000000ff);
    xfer += subReadString(name);
    xfer += subReadI32(seqid);
  } else {
    throw TProtocolException(TProtocolException::BAD_VERSION, "No version identifier... old protocol client in strict mode?");
  }
  return xfer;
}

uint32_t TDenseProtocol::readMessageEnd() {
  return 0;
}

uint32_t TDenseProtocol::readStructBegin(string& name) {
  // TODO(dreiss): Any chance this gets inlined?
  return TDenseProtocol::writeStructBegin(name);
}

uint32_t TDenseProtocol::readStructEnd() {
  idx_stack_.pop_back();
  stateTransition();
  return 0;
}

uint32_t TDenseProtocol::readFieldBegin(string& name,
                                        TType& fieldType,
                                        int16_t& fieldId) {
  uint32_t xfer = 0;

  while (FMT.is_optional) {
    bool is_present;
    xfer += subReadBool(is_present);
    if (is_present) {
      break;
    }
    IDX++;
  }

  fieldId   = FMT.tag;
  fieldType = FTS->ttype;

  // OMG I'm so gross.  XXX
  if (FTS->ttype != T_STOP) {
    ts_stack_.push_back(FTS);
  }
  return xfer;
}

uint32_t TDenseProtocol::readFieldEnd() {
  IDX++;
  return 0;
}

uint32_t TDenseProtocol::readMapBegin(TType& keyType,
                                      TType& valType,
                                      uint32_t& size) {
  checkTType(T_MAP);

  uint32_t xfer = 0;
  int32_t sizei;
  xfer += subReadI32(sizei);
  if (sizei < 0) {
    throw TProtocolException(TProtocolException::NEGATIVE_SIZE);
  } else if (container_limit_ && sizei > container_limit_) {
    throw TProtocolException(TProtocolException::SIZE_LIMIT);
  }
  size = (uint32_t)sizei;

  keyType = ST1->ttype;
  valType = ST2->ttype;

  ts_stack_.push_back(ST1);
  mkv_stack_.push_back(true);

  return xfer;
}

uint32_t TDenseProtocol::readMapEnd() {
  ts_stack_.pop_back();
  mkv_stack_.pop_back();
  stateTransition();
  return 0;
}

uint32_t TDenseProtocol::readListBegin(TType& elemType,
                                       uint32_t& size) {
  checkTType(T_LIST);

  uint32_t xfer = 0;
  int32_t sizei;
  xfer += subReadI32(sizei);
  if (sizei < 0) {
    throw TProtocolException(TProtocolException::NEGATIVE_SIZE);
  } else if (container_limit_ && sizei > container_limit_) {
    throw TProtocolException(TProtocolException::SIZE_LIMIT);
  }
  size = (uint32_t)sizei;

  elemType = ST1->ttype;

  ts_stack_.push_back(ST1);

  return xfer;
}

uint32_t TDenseProtocol::readListEnd() {
  ts_stack_.pop_back();
  stateTransition();
  return 0;
}

uint32_t TDenseProtocol::readSetBegin(TType& elemType,
                                      uint32_t& size) {
  checkTType(T_SET);

  uint32_t xfer = 0;
  int32_t sizei;
  xfer += subReadI32(sizei);
  if (sizei < 0) {
    throw TProtocolException(TProtocolException::NEGATIVE_SIZE);
  } else if (container_limit_ && sizei > container_limit_) {
    throw TProtocolException(TProtocolException::SIZE_LIMIT);
  }
  size = (uint32_t)sizei;

  elemType = ST1->ttype;

  ts_stack_.push_back(ST1);

  return xfer;
}

uint32_t TDenseProtocol::readSetEnd() {
  ts_stack_.pop_back();
  stateTransition();
  return 0;
}

uint32_t TDenseProtocol::readBool(bool& value) {
  checkTType(T_BOOL);
  stateTransition();
  return TBinaryProtocol::readBool(value);
}

uint32_t TDenseProtocol::readByte(int8_t& byte) {
  checkTType(T_BYTE);
  stateTransition();
  return TBinaryProtocol::readByte(byte);
}

uint32_t TDenseProtocol::readI16(int16_t& i16) {
  checkTType(T_I16);
  stateTransition();
  uint64_t u64;
  uint32_t rv = vliRead(u64);
  int64_t val = (int64_t)u64;
  if (UNLIKELY(val > INT16_MAX || val < INT16_MIN)) {
    throw TProtocolException(TProtocolException::INVALID_DATA,
                             "i16 out of range.");
  }
  i16 = (int16_t)val;
  return rv;
}

uint32_t TDenseProtocol::readI32(int32_t& i32) {
  checkTType(T_I32);
  stateTransition();
  uint64_t u64;
  uint32_t rv = vliRead(u64);
  int64_t val = (int64_t)u64;
  if (UNLIKELY(val > INT32_MAX || val < INT32_MIN)) {
    throw TProtocolException(TProtocolException::INVALID_DATA,
                             "i32 out of range.");
  }
  i32 = (int32_t)val;
  return rv;
}

uint32_t TDenseProtocol::readI64(int64_t& i64) {
  checkTType(T_I64);
  stateTransition();
  uint64_t u64;
  uint32_t rv = vliRead(u64);
  int64_t val = (int64_t)u64;
  if (UNLIKELY(val > INT64_MAX || val < INT64_MIN)) {
    throw TProtocolException(TProtocolException::INVALID_DATA,
                             "i64 out of range.");
  }
  i64 = (int64_t)val;
  return rv;
}

uint32_t TDenseProtocol::readDouble(double& dub) {
  checkTType(T_DOUBLE);
  stateTransition();
  return TBinaryProtocol::readDouble(dub);
}

uint32_t TDenseProtocol::readString(std::string& str) {
  checkTType(T_STRING);
  stateTransition();
  return subReadString(str);
}

uint32_t TDenseProtocol::subReadI32(int32_t& i32) {
  uint64_t u64;
  uint32_t rv = vliRead(u64);
  int64_t val = (int64_t)u64;
  if (UNLIKELY(val > INT32_MAX || val < INT32_MIN)) {
    throw TProtocolException(TProtocolException::INVALID_DATA,
                             "i32 out of range.");
  }
  i32 = (int32_t)val;
  return rv;
}

uint32_t TDenseProtocol::subReadString(std::string& str) {
  uint32_t xfer;
  int32_t size;
  xfer = subReadI32(size);
  return xfer + readStringBody(str, size);
}

}}} // facebook::thrift::protocol