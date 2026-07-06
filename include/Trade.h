#pragma once

// -----------------------------------------------------------------------------
// Trade.h
//
// Immutable record of a single execution between a buy order and a sell
// order. One incoming order can generate multiple Trade records if it
// sweeps through several price levels / resting orders.
// -----------------------------------------------------------------------------

struct Trade {
    long long tradeId;
    long long buyOrderId;
    long long sellOrderId;
    double price;
    int quantity;
    long long timestamp;

    Trade(long long tradeId_,
          long long buyOrderId_,
          long long sellOrderId_,
          double price_,
          int quantity_,
          long long timestamp_)
        : tradeId(tradeId_),
          buyOrderId(buyOrderId_),
          sellOrderId(sellOrderId_),
          price(price_),
          quantity(quantity_),
          timestamp(timestamp_) {}
};
