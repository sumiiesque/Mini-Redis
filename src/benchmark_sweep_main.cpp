// Parameter-sweep driver: builds the full cross-product of backends x
// thread-counts x read/write-ratios x zipf-thetas (see benchmark::SweepSpec
// / GenerateSweepCommands) and invokes the benchmark executable once per
// combination, all appending into the same output CSV.

#include <cstdio>
#include <cstdlib>
#include <sstream>
#include <string>
#include <vector>

#include "benchmark/sweep.h"

namespace {

std::vector<std::string> SplitCsv(const std::string& s) {
  std::vector<std::string> parts;
  std::stringstream ss(s);
  std::string item;
  while (std::getline(ss, item, ',')) {
    if (!item.empty()) parts.push_back(item);
  }
  return parts;
}

benchmark::ReadWriteRatio ParseRatio(const std::string& s) {
  auto pos = s.find(':');
  benchmark::ReadWriteRatio r{100, 0};
  if (pos == std::string::npos) return r;
  r.read_percent = std::atoi(s.substr(0, pos).c_str());
  r.write_percent = std::atoi(s.substr(pos + 1).c_str());
  return r;
}

void PrintUsage() {
  std::printf(
      "mini_redis_benchmark_sweep [flags]\n"
      "  --exe PATH                 path to the mini_redis_benchmark executable "
      "(default: ./mini_redis_benchmark)\n"
      "  --backends L,L,...         comma-separated (default: locking,mvcc)\n"
      "  --threads N,N,...          comma-separated (default: 1,4,8)\n"
      "  --ratios R:W,R:W,...       comma-separated read:write pairs (default: 100:0,50:50)\n"
      "  --thetas X,X,...           comma-separated (default: 0,0.99)\n"
      "  --keyspace N               (default: 10000)\n"
      "  --seed N                   (default: 42)\n"
      "  --warmup-seconds X         (default: 1.0)\n"
      "  --duration-seconds X       (default: 5.0)\n"
      "  --output PATH              (default: results.csv)\n"
      "\n"
      "Example (both backends, three thread counts, two ratios, two skews):\n"
      "  mini_redis_benchmark_sweep --exe ./build/src/mini_redis_benchmark "
      "--backends locking,mvcc --threads 1,4,8 --ratios 100:0,50:50 --thetas 0,0.99 "
      "--duration-seconds 5 --output sweep_results.csv\n");
}

}  // namespace

int main(int argc, char** argv) {
  std::string exe_path = "./mini_redis_benchmark";
  benchmark::SweepSpec spec;
  spec.backends = {"locking", "mvcc"};
  spec.thread_counts = {1, 4, 8};
  spec.read_write_ratios = {{100, 0}, {50, 50}};
  spec.zipf_thetas = {0.0, 0.99};

  for (int i = 1; i < argc; ++i) {
    std::string name = argv[i];
    if (name == "--help" || name == "-h") {
      PrintUsage();
      return 0;
    }
    if (i + 1 >= argc) {
      std::fprintf(stderr, "error: missing value for flag '%s'\n", name.c_str());
      return 1;
    }
    std::string value = argv[++i];

    if (name == "--exe") {
      exe_path = value;
    } else if (name == "--backends") {
      spec.backends = SplitCsv(value);
    } else if (name == "--threads") {
      spec.thread_counts.clear();
      for (auto& s : SplitCsv(value)) spec.thread_counts.push_back(std::atoi(s.c_str()));
    } else if (name == "--ratios") {
      spec.read_write_ratios.clear();
      for (auto& s : SplitCsv(value)) spec.read_write_ratios.push_back(ParseRatio(s));
    } else if (name == "--thetas") {
      spec.zipf_thetas.clear();
      for (auto& s : SplitCsv(value)) spec.zipf_thetas.push_back(std::atof(s.c_str()));
    } else if (name == "--keyspace") {
      spec.keyspace = std::atoll(value.c_str());
    } else if (name == "--seed") {
      spec.seed = static_cast<uint64_t>(std::atoll(value.c_str()));
    } else if (name == "--warmup-seconds") {
      spec.warmup_seconds = std::atof(value.c_str());
    } else if (name == "--duration-seconds") {
      spec.duration_seconds = std::atof(value.c_str());
    } else if (name == "--output") {
      spec.output_csv = value;
    } else {
      std::fprintf(stderr, "error: unknown flag '%s'\n", name.c_str());
      return 1;
    }
  }

  auto commands = benchmark::GenerateSweepCommands(spec);
  std::printf("Running %zu benchmark invocations, appending to %s\n", commands.size(),
              spec.output_csv.c_str());

  size_t failures = 0;
  for (size_t i = 0; i < commands.size(); ++i) {
    std::string cmd = "\"" + exe_path + "\"";
    for (const auto& tok : commands[i]) cmd += " \"" + tok + "\"";

    std::printf("[%zu/%zu] %s\n", i + 1, commands.size(), cmd.c_str());
    int rc = std::system(cmd.c_str());
    if (rc != 0) {
      std::fprintf(stderr, "  run failed (exit code %d)\n", rc);
      ++failures;
    }
  }

  std::printf("Sweep complete: %zu/%zu runs succeeded\n", commands.size() - failures,
              commands.size());
  return failures == 0 ? 0 : 1;
}
