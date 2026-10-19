#pragma once

#include <cstdio>
#include <memory>
#include <string>
#include <vector>

#include "engine/types.hpp"

namespace te {

// A stream of public trades in timestamp order. Backtests read files; live
// mode reads a WebSocket feed. The engine only sees this interface.
class DataSource {
public:
    virtual ~DataSource() = default;
    virtual bool next(MarketTrade& out) = 0;
};

class VectorSource : public DataSource {
public:
    explicit VectorSource(std::vector<MarketTrade> trades) : trades_(std::move(trades)) {}
    bool next(MarketTrade& out) override {
        if (pos_ >= trades_.size()) return false;
        out = trades_[pos_++];
        return true;
    }

private:
    std::vector<MarketTrade> trades_;
    std::size_t pos_ = 0;
};

// Streams a trades CSV without loading it into memory. Accepts:
//  * Binance public dumps (data.binance.vision, spot "trades"), no header:
//      id,price,qty,quote_qty,time,is_buyer_maker,is_best_match
//    `time` may be milliseconds (older files) or microseconds (2025+); both are detected.
//  * The project's normalized format, with header:
//      ts_us,price,qty,buyer_is_maker[,trade_id]
class CsvTradeSource : public DataSource {
public:
    explicit CsvTradeSource(const std::string& path);
    ~CsvTradeSource() override;
    CsvTradeSource(const CsvTradeSource&) = delete;
    CsvTradeSource& operator=(const CsvTradeSource&) = delete;

    bool next(MarketTrade& out) override;
    std::size_t lines_skipped() const { return skipped_; }

private:
    enum class Format { Unknown, Binance, Normalized };
    bool parse_line(const char* line, std::size_t len, MarketTrade& out);

    std::FILE* file_ = nullptr;
    char* buf_ = nullptr;
    std::size_t cap_ = 0;
    Format format_ = Format::Unknown;
    std::size_t skipped_ = 0;
};

// Converts an exchange timestamp of unknown resolution (s, ms, us, ns) to microseconds.
Timestamp normalize_timestamp(std::int64_t raw);

}  // namespace te
