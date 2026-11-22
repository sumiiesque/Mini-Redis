#include "benchmark/config.h"

#include <cstdlib>

namespace benchmark {
namespace {

// Splits "--name=value" into ("name", "value"); returns false if `arg`
// isn't of that shape (caller then treats the next argv as the value).
bool SplitEquals(const std::string& arg, std::string* name, std::string* value) {
  auto pos = arg.find('=');
  if (pos == std::string::npos) return false;
  *name = arg.substr(0, pos);
  *value = arg.substr(pos + 1);
  return true;
}

bool ParseInt(const std::string& s, int64_t* out) {
  if (s.empty()) return false;
  char* end = nullptr;
  int64_t v = std::strtoll(s.c_str(), &end, 10);
  if (end != s.c_str() + s.size()) return false;
  *out = v;
  return true;
}

bool ParseDouble(const std::string& s, double* out) {
  if (s.empty()) return false;
  char* end = nullptr;
  double v = std::strtod(s.c_str(), &end);
  if (end != s.c_str() + s.size()) return false;
  *out = v;
  return true;
}

}  // namespace

ParsedArgs ParseArgs(int argc, char** argv) {
  ParsedArgs result;
  BenchmarkConfig cfg;

  for (int i = 1; i < argc; ++i) {
    std::string arg = argv[i];
    if (arg == "--help" || arg == "-h") {
      result.outcome = ParseOutcome::kHelpRequested;
      return result;
    }

    std::string name, value;
    bool has_value = SplitEquals(arg, &name, &value);
    if (!has_value) {
      name = arg;
      if (i + 1 >= argc) {
        result.error = "missing value for flag '" + name + "'";
        return result;
      }
      value = argv[++i];
    }

    int64_t ival = 0;
    double dval = 0.0;

    if (name == "--backend") {
      if (value != "locking" && value != "mvcc") {
        result.error = "--backend must be 'locking' or 'mvcc', got '" + value + "'";
        return result;
      }
      cfg.backend = value;
    } else if (name == "--threads") {
      if (!ParseInt(value, &ival)) {
        result.error = "--threads expects an integer, got '" + value + "'";
        return result;
      }
      cfg.threads = static_cast<int>(ival);
    } else if (name == "--read-percent") {
      if (!ParseInt(value, &ival)) {
        result.error = "--read-percent expects an integer, got '" + value + "'";
        return result;
      }
      cfg.read_percent = static_cast<int>(ival);
    } else if (name == "--write-percent") {
      if (!ParseInt(value, &ival)) {
        result.error = "--write-percent expects an integer, got '" + value + "'";
        return result;
      }
      cfg.write_percent = static_cast<int>(ival);
    } else if (name == "--keyspace") {
      if (!ParseInt(value, &ival)) {
        result.error = "--keyspace expects an integer, got '" + value + "'";
        return result;
      }
      cfg.keyspace = ival;
    } else if (name == "--zipf-theta") {
      if (!ParseDouble(value, &dval)) {
        result.error = "--zipf-theta expects a number, got '" + value + "'";
        return result;
      }
      cfg.zipf_theta = dval;
    } else if (name == "--seed") {
      if (!ParseInt(value, &ival)) {
        result.error = "--seed expects an integer, got '" + value + "'";
        return result;
      }
      cfg.seed = static_cast<uint64_t>(ival);
    } else if (name == "--warmup-seconds") {
      if (!ParseDouble(value, &dval)) {
        result.error = "--warmup-seconds expects a number, got '" + value + "'";
        return result;
      }
      cfg.warmup_seconds = dval;
    } else if (name == "--duration-seconds") {
      if (!ParseDouble(value, &dval)) {
        result.error = "--duration-seconds expects a number, got '" + value + "'";
        return result;
      }
      cfg.duration_seconds = dval;
    } else if (name == "--output") {
      cfg.output_csv = value;
    } else {
      result.error = "unknown flag '" + name + "'";
      return result;
    }
  }

  result.outcome = ParseOutcome::kOk;
  result.config = cfg;
  return result;
}

bool ValidateConfig(const BenchmarkConfig& cfg, std::string* error) {
  if (cfg.backend != "locking" && cfg.backend != "mvcc") {
    *error = "backend must be 'locking' or 'mvcc'";
    return false;
  }
  if (cfg.threads < 1) {
    *error = "threads must be >= 1";
    return false;
  }
  if (cfg.keyspace < 1) {
    *error = "keyspace must be >= 1";
    return false;
  }
  if (cfg.read_percent < 0 || cfg.read_percent > 100) {
    *error = "read-percent must be in [0, 100]";
    return false;
  }
  if (cfg.write_percent < 0 || cfg.write_percent > 100) {
    *error = "write-percent must be in [0, 100]";
    return false;
  }
  if (cfg.read_percent + cfg.write_percent != 100) {
    *error = "read-percent + write-percent must equal 100";
    return false;
  }
  if (cfg.zipf_theta < 0.0) {
    *error = "zipf-theta must be >= 0 (0 means uniform distribution)";
    return false;
  }
  if (cfg.warmup_seconds < 0.0) {
    *error = "warmup-seconds must be >= 0";
    return false;
  }
  if (cfg.duration_seconds <= 0.0) {
    *error = "duration-seconds must be > 0";
    return false;
  }
  if (cfg.output_csv.empty()) {
    *error = "output path must not be empty";
    return false;
  }
  return true;
}

}  // namespace benchmark
