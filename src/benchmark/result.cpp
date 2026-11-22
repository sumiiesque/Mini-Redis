#include "benchmark/result.h"

#include <fstream>
#include <sstream>
#include <sys/stat.h>

namespace benchmark {

const char kCsvHeader[] =
    "backend,threads,read_percent,write_percent,keyspace,zipf_theta,seed,"
    "warmup_seconds,duration_seconds,operations,throughput_ops_sec,"
    "p99_latency_us,memory_mb,abort_count,abort_rate";

std::string FormatCsvRow(const BenchmarkResult& r) {
  std::ostringstream out;
  out << r.config.backend << ',' << r.config.threads << ',' << r.config.read_percent << ','
      << r.config.write_percent << ',' << r.config.keyspace << ',' << r.config.zipf_theta << ','
      << r.config.seed << ',' << r.config.warmup_seconds << ',' << r.config.duration_seconds
      << ',' << r.operations << ',' << r.throughput_ops_sec << ',' << r.p99_latency_us << ','
      << r.memory_mb << ',' << r.abort_count << ',' << r.abort_rate;
  return out.str();
}

namespace {
bool FileExistsAndNonEmpty(const std::string& path) {
  struct stat st{};
  return ::stat(path.c_str(), &st) == 0 && st.st_size > 0;
}
}  // namespace

void AppendCsvRow(const std::string& path, const BenchmarkResult& result) {
  bool need_header = !FileExistsAndNonEmpty(path);
  std::ofstream out(path, std::ios::app);
  if (need_header) out << kCsvHeader << '\n';
  out << FormatCsvRow(result) << '\n';
}

}  // namespace benchmark
