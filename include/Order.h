#pragma once

// -----------------------------------------------------------------------------
// Order.h
//
// Represents a single BUY or SELL order submitted to the exchange.
// The class is intentionally lightweight and header-only: it has no external
// dependencies and all state transitions are encapsulated behind small,
// well-named methods so the OrderBook never manipulates raw fields directly.
// -----------------------------------------------------------------------------

// Direction of an order.
enum class Side {
    BUY,
    SELL
};

// Lifecycle state of an order.
enum class OrderStatus {
    ACTIVE,            // Fully open, no quantity filled yet.
    PARTIALLY_FILLED,  // Some quantity has been matched, remainder still open.
    FILLED,            // Fully matched, no remaining quantity.
    CANCELLED          // Removed from the book before being fully filled.
};

class Order {
public:
    Order(long long id, double price, int quantity, Side side, long long timestamp)
        : id_(id),
          price_(price),
          quantity_(quantity),
          remainingQuantity_(quantity),
          side_(side),
          timestamp_(timestamp),
          status_(OrderStatus::ACTIVE) {}

    // --- Accessors ---
    long long getId() const { return id_; }
    double getPrice() const { return price_; }
    int getQuantity() const { return quantity_; }
    int getRemainingQuantity() const { return remainingQuantity_; }
    Side getSide() const { return side_; }
    long long getTimestamp() const { return timestamp_; }
    OrderStatus getStatus() const { return status_; }

    // --- Mutators ---
    void setStatus(OrderStatus status) { status_ = status; }
    void setTimestamp(long long timestamp) { timestamp_ = timestamp; }

    // Applies a fill of `qty` units against this order and updates its status
    // automatically. `qty` must never exceed remainingQuantity_; the OrderBook
    // is responsible for only ever passing a valid, clamped quantity.
    void fill(int qty) {
        remainingQuantity_ -= qty;
        if (remainingQuantity_ <= 0) {
            remainingQuantity_ = 0;
            status_ = OrderStatus::FILLED;
        } else {
            status_ = OrderStatus::PARTIALLY_FILLED;
        }
    }

    // An order is tradeable / displayable while it is ACTIVE or PARTIALLY_FILLED.
    bool isActive() const {
        return status_ == OrderStatus::ACTIVE || status_ == OrderStatus::PARTIALLY_FILLED;
    }

private:
    long long id_;
    double price_;
    int quantity_;             // Original quantity requested.
    int remainingQuantity_;    // Quantity still unfilled.
    Side side_;
    long long timestamp_;      // Logical timestamp used to enforce FIFO ordering.
    OrderStatus status_;
};
