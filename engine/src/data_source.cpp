#include "engine/data_source.hpp"

#include <array>
#include <cctype>
#include <charconv>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <string_view>

namespace te {

Timestamp normalize_timestamp(std::int64_t raw) {
    if (raw > 100'000'000'000'000'000LL) return raw / 1000;  // nanoseconds
    if (raw > 100'000'000'000'000LL) return raw;             // microseconds
    if (raw > 100'000'000'000LL) return raw * 1000;          // milliseconds
    return raw * 1'000'000;                                  // seconds
}

namespace {

constexpr std::size_t kMaxFields = 8;

std::size_t split(std::string_view line, std::array<std::string_view, kMaxFields>& fields) {
    std::size_t n = 0;
    std::size_t start = 0;
    while (n < kMaxFields) {
        const std::size_t comma = line.find(',', start);
        if (comma == std::string_view::npos) {
            fields[n++] = line.substr(start);
            break;
        }
        fields[n++] = line.substr(start, comma - start);
        start = comma + 1;
    }
    return n;
}

bool parse_double(std::string_view s, double& out) {
    auto res = std::from_chars(s.data(), s.data() + s.size(), out);
    return res.ec == std::errc{};
}

bool parse_int(std::string_view s, std::int64_t& out) {
    auto res = std::from_chars(s.data(), s.data() + s.size(), out);
    return res.ec == std::errc{};
}

bool parse_bool(std::string_view s, bool& out) {
    if (s == "True" || s == "true" || s == "1" || s == "TRUE") {
        out = true;
        return true;
    }
    if (s == "False" || s == "false" || s == "0" || s == "FALSE") {
        out = false;
        return true;
    }
    return false;
}

std::string_view trim(std::string_view s) {
    while (!s.empty() && (s.back() == '\n' || s.back() == '\r' || s.back() == ' ')) s.remove_suffix(1);
    while (!s.empty() && s.front() == ' ') s.remove_prefix(1);
    return s;
}

}  // namespace

CsvTradeSource::CsvTradeSource(const std::string& path) {
    file_ = std::fopen(path.c_str(), "r");
    if (!file_) throw std::runtime_error("cannot open trade file: " + path);
}

CsvTradeSource::~CsvTradeSource() {
    if (file_) std::fclose(file_);
    std::free(buf_);
}

bool CsvTradeSource::parse_line(const char* line, std::size_t len, MarketTrade& out) {
    std::string_view sv = trim(std::string_view(line, len));
    if (sv.empty()) return false;

    std::array<std::string_view, kMaxFields> f{};
    const std::size_t n = split(sv, f);

    if (format_ == Format::Unknown) {
        if (!std::isdigit(static_cast<unsigned char>(sv.front()))) {
            // Header line: decide the format from it and skip it.
            format_ = sv.find("ts_us") != std::string_view::npos ? Format::Normalized : Format::Binance;
            return false;
        }
        format_ = n >= 6 ? Format::Binance : Format::Normalized;
    }

    std::int64_t raw_ts = 0;
    if (format_ == Format::Binance) {
        if (n < 6) return false;
        std::int64_t id = 0;
        if (!parse_int(f[0], id) || !parse_double(f[1], out.price) || !parse_double(f[2], out.qty) ||
            !parse_int(f[4], raw_ts) || !parse_bool(f[5], out.buyer_is_maker))
            return false;
        out.trade_id = static_cast<std::uint64_t>(id);
    } else {
        if (n < 4) return false;
        if (!parse_int(f[0], raw_ts) || !parse_double(f[1], out.price) || !parse_double(f[2], out.qty) ||
            !parse_bool(f[3], out.buyer_is_maker))
            return false;
        std::int64_t id = 0;
        out.trade_id = (n >= 5 && parse_int(f[4], id)) ? static_cast<std::uint64_t>(id) : 0;
    }
    out.ts = normalize_timestamp(raw_ts);
    return out.price > 0.0 && out.qty > 0.0;
}

bool CsvTradeSource::next(MarketTrade& out) {
    ssize_t len;
    while ((len = getline(&buf_, &cap_, file_)) != -1) {
        const bool was_unknown = format_ == Format::Unknown;
        if (parse_line(buf_, static_cast<std::size_t>(len), out)) return true;
        if (!(was_unknown && format_ != Format::Unknown)) ++skipped_;  // don't count the header
    }
    return false;
}

}  // namespace te
