#include "benchmark/metrics.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <sstream>

namespace benchmark {

uint64_t PercentileP99(std::vector<uint64_t>* latencies_us) {
  if (latencies_us == nullptr || latencies_us->empty()) return 0;
  std::sort(latencies_us->begin(), latencies_us->end());
  size_t n = latencies_us->size();
  size_t idx = static_cast<size_t>(std::ceil(0.99 * static_cast<double>(n))) - 1;
  if (idx >= n) idx = n - 1;
  return (*latencies_us)[idx];
}

MemoryReading ReadProcessResidentMemory() {
  MemoryReading reading;
  std::ifstream f("/proc/self/status");
  if (!f.is_open()) return reading;

  std::string line;
  while (std::getline(f, line)) {
    if (line.compare(0, 6, "VmRSS:") != 0) continue;
    std::istringstream iss(line.substr(6));
    double kb = 0.0;
    if (!(iss >> kb)) return reading;  // malformed field: leave ok == false
    reading.resident_mb = kb / 1024.0;
    reading.ok = true;
    return reading;
  }
  return reading;  // field not found: ok remains false
}

}  // namespace benchmark
