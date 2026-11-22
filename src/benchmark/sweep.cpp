#include "benchmark/sweep.h"

namespace benchmark {

std::vector<std::vector<std::string>> GenerateSweepCommands(const SweepSpec& spec) {
  std::vector<std::vector<std::string>> commands;

  for (const auto& backend : spec.backends) {
    for (int threads : spec.thread_counts) {
      for (const auto& ratio : spec.read_write_ratios) {
        for (double theta : spec.zipf_thetas) {
          std::vector<std::string> args = {
              "--backend",
              backend,
              "--threads",
              std::to_string(threads),
              "--read-percent",
              std::to_string(ratio.read_percent),
              "--write-percent",
              std::to_string(ratio.write_percent),
              "--keyspace",
              std::to_string(spec.keyspace),
              "--zipf-theta",
              std::to_string(theta),
              "--seed",
              std::to_string(spec.seed),
              "--warmup-seconds",
              std::to_string(spec.warmup_seconds),
              "--duration-seconds",
              std::to_string(spec.duration_seconds),
              "--output",
              spec.output_csv,
          };
          commands.push_back(std::move(args));
        }
      }
    }
  }

  return commands;
}

}  // namespace benchmark
