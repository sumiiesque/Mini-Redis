#include "persistence/mutation_record.h"

#include <cstdio>
#include <string>

namespace {
int g_failures = 0;
int g_checks = 0;
}  // namespace

#define CHECK(cond)                                                                  \
  do {                                                                               \
    ++g_checks;                                                                      \
    if (!(cond)) {                                                                   \
      ++g_failures;                                                                  \
      std::fprintf(stderr, "CHECK FAILED: %s (%s:%d)\n", #cond, __FILE__, __LINE__); \
    }                                                                                \
  } while (0)

using persistence::DecodeStatus;
using persistence::MutationRecord;
using persistence::MutationStreamDecoder;
using persistence::MutationType;
using persistence::SerializeMutationRecord;

void TestRoundTripSetWithoutTtl() {
  MutationRecord r;
  r.type = MutationType::kSet;
  r.key = "k";
  r.value = "v";
  std::string wire = SerializeMutationRecord(r);

  MutationStreamDecoder d;
  d.Feed(wire.data(), wire.size());
  MutationRecord out;
  CHECK(d.TryDecode(&out) == DecodeStatus::kOk);
  CHECK(out.type == MutationType::kSet && out.key == "k" && out.value == "v" && !out.has_expire);
}

void TestRoundTripSetWithTtl() {
  MutationRecord r;
  r.type = MutationType::kSet;
  r.key = "k";
  r.value = "v";
  r.has_expire = true;
  r.expire_at_ms = 1234567890123LL;
  std::string wire = SerializeMutationRecord(r);

  MutationStreamDecoder d;
  d.Feed(wire.data(), wire.size());
  MutationRecord out;
  CHECK(d.TryDecode(&out) == DecodeStatus::kOk);
  CHECK(out.has_expire && out.expire_at_ms == 1234567890123LL);
}

void TestRoundTripDel() {
  MutationRecord r;
  r.type = MutationType::kDel;
  r.key = "gone";
  std::string wire = SerializeMutationRecord(r);

  MutationStreamDecoder d;
  d.Feed(wire.data(), wire.size());
  MutationRecord out;
  CHECK(d.TryDecode(&out) == DecodeStatus::kOk);
  CHECK(out.type == MutationType::kDel && out.key == "gone");
}

void TestRoundTripExpire() {
  MutationRecord r;
  r.type = MutationType::kExpire;
  r.key = "k";
  r.expire_at_ms = 42;
  std::string wire = SerializeMutationRecord(r);

  MutationStreamDecoder d;
  d.Feed(wire.data(), wire.size());
  MutationRecord out;
  CHECK(d.TryDecode(&out) == DecodeStatus::kOk);
  CHECK(out.type == MutationType::kExpire && out.has_expire && out.expire_at_ms == 42);
}

void TestEmptyKeyAndValue() {
  MutationRecord r;
  r.type = MutationType::kSet;
  r.key = "";
  r.value = "";
  std::string wire = SerializeMutationRecord(r);

  MutationStreamDecoder d;
  d.Feed(wire.data(), wire.size());
  MutationRecord out;
  CHECK(d.TryDecode(&out) == DecodeStatus::kOk);
  CHECK(out.key.empty() && out.value.empty());
}

void TestBinarySafeKeyAndValue() {
  MutationRecord r;
  r.type = MutationType::kSet;
  r.key = std::string("k\r\n\0withnul", 11);
  r.value = std::string("*3\r\n$3\r\nfoo\r\n", 14);  // looks like RESP, must not confuse framing
  std::string wire = SerializeMutationRecord(r);

  MutationStreamDecoder d;
  d.Feed(wire.data(), wire.size());
  MutationRecord out;
  CHECK(d.TryDecode(&out) == DecodeStatus::kOk);
  CHECK(out.key == r.key && out.value == r.value);
}

void TestMultipleRecordsInOneBuffer() {
  MutationRecord a;
  a.type = MutationType::kSet;
  a.key = "a";
  a.value = "1";
  MutationRecord b;
  b.type = MutationType::kDel;
  b.key = "b";

  std::string wire = SerializeMutationRecord(a) + SerializeMutationRecord(b);
  MutationStreamDecoder d;
  d.Feed(wire.data(), wire.size());

  MutationRecord out1, out2;
  CHECK(d.TryDecode(&out1) == DecodeStatus::kOk);
  CHECK(out1.key == "a");
  CHECK(d.TryDecode(&out2) == DecodeStatus::kOk);
  CHECK(out2.key == "b");
  CHECK(d.TryDecode(&out2) == DecodeStatus::kIncomplete);
}

void TestFragmentedAcrossManyFeeds() {
  MutationRecord r;
  r.type = MutationType::kSet;
  r.key = "frag";
  r.value = "mented-value";
  std::string wire = SerializeMutationRecord(r);

  MutationStreamDecoder d;
  for (char c : wire) {
    MutationRecord probe;
    CHECK(d.TryDecode(&probe) == DecodeStatus::kIncomplete);
    d.Feed(&c, 1);
  }
  MutationRecord out;
  CHECK(d.TryDecode(&out) == DecodeStatus::kOk);
  CHECK(out.key == "frag" && out.value == "mented-value");
}

void TestTruncatedRecordIsIncomplete() {
  MutationRecord r;
  r.type = MutationType::kSet;
  r.key = "k";
  r.value = "value";
  std::string wire = SerializeMutationRecord(r);

  MutationStreamDecoder d;
  d.Feed(wire.data(), wire.size() - 3);  // chop off the tail
  MutationRecord out;
  CHECK(d.TryDecode(&out) == DecodeStatus::kIncomplete);
}

void TestMalformedType() {
  MutationRecord r;
  r.type = MutationType::kSet;
  r.key = "k";
  r.value = "v";
  std::string wire = SerializeMutationRecord(r);
  wire[4] = '\x7f';  // corrupt the type byte (right after the 4-byte length prefix)

  MutationStreamDecoder d;
  d.Feed(wire.data(), wire.size());
  MutationRecord out;
  CHECK(d.TryDecode(&out) == DecodeStatus::kMalformed);
}

void TestMalformedInternalLength() {
  MutationRecord r;
  r.type = MutationType::kSet;
  r.key = "k";
  r.value = "v";
  std::string wire = SerializeMutationRecord(r);
  // Corrupt the key-length field (first 4 bytes of the body, right after the
  // 4-byte outer length prefix + 1 type byte) to claim a key longer than the
  // record actually contains.
  wire[5] = '\xff';
  wire[6] = '\xff';

  MutationStreamDecoder d;
  d.Feed(wire.data(), wire.size());
  MutationRecord out;
  CHECK(d.TryDecode(&out) == DecodeStatus::kMalformed);
}

void TestOversizedDeclaredLengthRejected() {
  std::string wire;
  wire.push_back('\xff');
  wire.push_back('\xff');
  wire.push_back('\xff');
  wire.push_back('\x7f');  // declares a ~2GB body: must be rejected before any allocation

  MutationStreamDecoder d;
  d.Feed(wire.data(), wire.size());
  MutationRecord out;
  CHECK(d.TryDecode(&out) == DecodeStatus::kMalformed);
}

int main() {
  TestRoundTripSetWithoutTtl();
  TestRoundTripSetWithTtl();
  TestRoundTripDel();
  TestRoundTripExpire();
  TestEmptyKeyAndValue();
  TestBinarySafeKeyAndValue();
  TestMultipleRecordsInOneBuffer();
  TestFragmentedAcrossManyFeeds();
  TestTruncatedRecordIsIncomplete();
  TestMalformedType();
  TestMalformedInternalLength();
  TestOversizedDeclaredLengthRejected();

  std::printf("%d/%d checks passed\n", g_checks - g_failures, g_checks);
  return g_failures == 0 ? 0 : 1;
}
