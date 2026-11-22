#include "benchmark/sweep.h"

#include <algorithm>
#include <cstdio>

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

using benchmark::GenerateSweepCommands;
using benchmark::SweepSpec;

bool ContainsFlagValue(const std::vector<std::string>& args, const std::string& flag,
                       const std::string& value) {
  for (size_t i = 0; i + 1 < args.size(); ++i) {
    if (args[i] == flag && args[i + 1] == value) return true;
  }
  return false;
}

void TestCombinationCountIsCrossProduct() {
  SweepSpec spec;
  spec.backends = {"locking", "mvcc"};
  spec.thread_counts = {1, 4, 8};
  spec.read_write_ratios = {{100, 0}, {50, 50}};
  spec.zipf_thetas = {0.0, 0.99};

  auto commands = GenerateSweepCommands(spec);
  CHECK(commands.size() == 2 * 3 * 2 * 2);  // 24
}

void TestEveryCombinationHasBackendFlag() {
  SweepSpec spec;
  spec.backends = {"locking", "mvcc"};
  spec.thread_counts = {1};
  spec.read_write_ratios = {{100, 0}};
  spec.zipf_thetas = {0.5};

  auto commands = GenerateSweepCommands(spec);
  CHECK(commands.size() == 2);
  CHECK(ContainsFlagValue(commands[0], "--backend", "locking"));
  CHECK(ContainsFlagValue(commands[1], "--backend", "mvcc"));
}

void TestBothBackendsGetIdenticalNonBackendFlags() {
  SweepSpec spec;
  spec.backends = {"locking", "mvcc"};
  spec.thread_counts = {4};
  spec.read_write_ratios = {{70, 30}};
  spec.zipf_thetas = {0.99};
  spec.keyspace = 5000;

  auto commands = GenerateSweepCommands(spec);
  CHECK(commands.size() == 2);
  for (auto& cmd : commands) {
    CHECK(ContainsFlagValue(cmd, "--threads", "4"));
    CHECK(ContainsFlagValue(cmd, "--read-percent", "70"));
    CHECK(ContainsFlagValue(cmd, "--write-percent", "30"));
    CHECK(ContainsFlagValue(cmd, "--keyspace", "5000"));
  }
}

void TestSingleCombination() {
  SweepSpec spec;
  spec.backends = {"locking"};
  spec.thread_counts = {1};
  spec.read_write_ratios = {{100, 0}};
  spec.zipf_thetas = {0.0};

  auto commands = GenerateSweepCommands(spec);
  CHECK(commands.size() == 1);
}

void TestEmptyListsProduceNoCommands() {
  SweepSpec spec;
  spec.backends = {};
  spec.thread_counts = {1, 2};
  spec.read_write_ratios = {{100, 0}};
  spec.zipf_thetas = {0.0};

  auto commands = GenerateSweepCommands(spec);
  CHECK(commands.empty());
}

int main() {
  TestCombinationCountIsCrossProduct();
  TestEveryCombinationHasBackendFlag();
  TestBothBackendsGetIdenticalNonBackendFlags();
  TestSingleCombination();
  TestEmptyListsProduceNoCommands();

  std::printf("%d/%d checks passed\n", g_checks - g_failures, g_checks);
  return g_failures == 0 ? 0 : 1;
}
