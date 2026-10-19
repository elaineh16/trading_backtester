#include "engine/portfolio.hpp"

#include <algorithm>
#include <cmath>

namespace te {

void Portfolio::apply_fill(const Fill& f) {
    const double signed_qty = sign(f.side) * f.qty;
    cash_ -= signed_qty * f.price + f.fee;
    fees_ += f.fee;
    traded_notional_ += f.qty * f.price;
    ++fill_count_;

    const double old_pos = position_;
    const double new_pos = old_pos + signed_qty;

    if (old_pos == 0.0 || (old_pos > 0) == (signed_qty > 0)) {
        // Opening or adding: blend the average entry price.
        avg_price_ = (old_pos * avg_price_ + signed_qty * f.price) / new_pos;
    } else {
        // Reducing, closing or flipping.
        const double closed = std::min(std::fabs(signed_qty), std::fabs(old_pos));
        realized_pnl_ += closed * (f.price - avg_price_) * (old_pos > 0 ? 1.0 : -1.0);
        if (std::fabs(new_pos) < 1e-12) {
            avg_price_ = 0.0;
        } else if ((new_pos > 0) != (old_pos > 0)) {
            avg_price_ = f.price;  // flipped: remainder opened at the fill price
        }
    }

    position_ = std::fabs(new_pos) < 1e-12 ? 0.0 : new_pos;
    max_abs_position_ = std::max(max_abs_position_, std::fabs(position_));
    if (mark_price_ == 0.0) mark_price_ = f.price;
}

void Portfolio::mark(Timestamp /*ts*/, double price) { mark_price_ = price; }

void Portfolio::record_equity(Timestamp ts) { curve_.push_back({ts, equity(), position_, mark_price_}); }

}  // namespace te
