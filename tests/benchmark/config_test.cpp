#include "benchmark/config.h"

#include <cstdio>
#include <string>
#include <vector>

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

using benchmark::BenchmarkConfig;
using benchmark::ParseArgs;
using benchmark::ParseOutcome;
using benchmark::ValidateConfig;

std::vector<char*> ToArgv(std::vector<std::string>& storage) {
  std::vector<char*> argv;
  for (auto& s : storage) argv.push_back(s.data());
  return argv;
}

void TestParseDefaults() {
  std::vector<std::string> args = {"prog"};
  auto argv = ToArgv(args);
  auto parsed = ParseArgs(static_cast<int>(argv.size()), argv.data());
  CHECK(parsed.outcome == ParseOutcome::kOk);
  CHECK(parsed.config.backend == "locking");
}

void TestParseAllFlagsSpaceSeparated() {
  std::vector<std::string> args = {"prog",     "--backend", "mvcc",   "--threads",
                                    "8",        "--read-percent", "70", "--write-percent",
                                    "30",       "--keyspace", "5000",  "--zipf-theta",
                                    "0.5",      "--seed",    "123",    "--warmup-seconds",
                                    "2.5",      "--duration-seconds", "10", "--output",
                                    "out.csv"};
  auto argv = ToArgv(args);
  auto parsed = ParseArgs(static_cast<int>(argv.size()), argv.data());
  CHECK(parsed.outcome == ParseOutcome::kOk);
  CHECK(parsed.config.backend == "mvcc");
  CHECK(parsed.config.threads == 8);
  CHECK(parsed.config.read_percent == 70);
  CHECK(parsed.config.write_percent == 30);
  CHECK(parsed.config.keyspace == 5000);
  CHECK(parsed.config.zipf_theta == 0.5);
  CHECK(parsed.config.seed == 123);
  CHECK(parsed.config.warmup_seconds == 2.5);
  CHECK(parsed.config.duration_seconds == 10);
  CHECK(parsed.config.output_csv == "out.csv");
}

void TestParseEqualsForm() {
  std::vector<std::string> args = {"prog", "--backend=mvcc", "--threads=4"};
  auto argv = ToArgv(args);
  auto parsed = ParseArgs(static_cast<int>(argv.size()), argv.data());
  CHECK(parsed.outcome == ParseOutcome::kOk);
  CHECK(parsed.config.backend == "mvcc");
  CHECK(parsed.config.threads == 4);
}

void TestParseHelp() {
  std::vector<std::string> args = {"prog", "--help"};
  auto argv = ToArgv(args);
  auto parsed = ParseArgs(static_cast<int>(argv.size()), argv.data());
  CHECK(parsed.outcome == ParseOutcome::kHelpRequested);
}

void TestParseUnknownFlag() {
  std::vector<std::string> args = {"prog", "--bogus", "1"};
  auto argv = ToArgv(args);
  auto parsed = ParseArgs(static_cast<int>(argv.size()), argv.data());
  CHECK(parsed.outcome == ParseOutcome::kError);
}

void TestParseInvalidBackend() {
  std::vector<std::string> args = {"prog", "--backend", "redis"};
  auto argv = ToArgv(args);
  auto parsed = ParseArgs(static_cast<int>(argv.size()), argv.data());
  CHECK(parsed.outcome == ParseOutcome::kError);
}

void TestParseNonNumeric() {
  std::vector<std::string> args = {"prog", "--threads", "four"};
  auto argv = ToArgv(args);
  auto parsed = ParseArgs(static_cast<int>(argv.size()), argv.data());
  CHECK(parsed.outcome == ParseOutcome::kError);
}

void TestParseMissingValue() {
  std::vector<std::string> args = {"prog", "--threads"};
  auto argv = ToArgv(args);
  auto parsed = ParseArgs(static_cast<int>(argv.size()), argv.data());
  CHECK(parsed.outcome == ParseOutcome::kError);
}

void TestValidateGoodConfig() {
  BenchmarkConfig cfg;
  std::string error;
  CHECK(ValidateConfig(cfg, &error));
}

void TestValidateRejectsBadThreads() {
  BenchmarkConfig cfg;
  cfg.threads = 0;
  std::string error;
  CHECK(!ValidateConfig(cfg, &error));
}

void TestValidateRejectsBadKeyspace() {
  BenchmarkConfig cfg;
  cfg.keyspace = 0;
  std::string error;
  CHECK(!ValidateConfig(cfg, &error));
}

void TestValidateRejectsPercentagesNotSummingTo100() {
  BenchmarkConfig cfg;
  cfg.read_percent = 60;
  cfg.write_percent = 60;
  std::string error;
  CHECK(!ValidateConfig(cfg, &error));
}

void TestValidateRejectsOutOfRangePercentages() {
  BenchmarkConfig cfg;
  cfg.read_percent = -1;
  cfg.write_percent = 101;
  std::string error;
  CHECK(!ValidateConfig(cfg, &error));
}

void TestValidateRejectsNegativeTheta() {
  BenchmarkConfig cfg;
  cfg.zipf_theta = -0.1;
  std::string error;
  CHECK(!ValidateConfig(cfg, &error));
}

void TestValidateAcceptsThetaZero() {
  BenchmarkConfig cfg;
  cfg.zipf_theta = 0.0;
  std::string error;
  CHECK(ValidateConfig(cfg, &error));
}

void TestValidateRejectsNonPositiveDuration() {
  BenchmarkConfig cfg;
  cfg.duration_seconds = 0.0;
  std::string error;
  CHECK(!ValidateConfig(cfg, &error));
}

int main() {
  TestParseDefaults();
  TestParseAllFlagsSpaceSeparated();
  TestParseEqualsForm();
  TestParseHelp();
  TestParseUnknownFlag();
  TestParseInvalidBackend();
  TestParseNonNumeric();
  TestParseMissingValue();
  TestValidateGoodConfig();
  TestValidateRejectsBadThreads();
  TestValidateRejectsBadKeyspace();
  TestValidateRejectsPercentagesNotSummingTo100();
  TestValidateRejectsOutOfRangePercentages();
  TestValidateRejectsNegativeTheta();
  TestValidateAcceptsThetaZero();
  TestValidateRejectsNonPositiveDuration();

  std::printf("%d/%d checks passed\n", g_checks - g_failures, g_checks);
  return g_failures == 0 ? 0 : 1;
}
