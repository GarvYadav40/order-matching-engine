#pragma once

#include <map>
#include <unordered_map>
#include <deque>
#include <vector>
#include <memory>
#include <cstddef>
#include <functional>   // std::greater
#include <string>       // std::string

#include "Order.h"
#include "Trade.h"

// -----------------------------------------------------------------------------
// OrderBook.h
//
// Core matching engine. Maintains two price-ordered ladders (bids and asks),
// an O(1) lookup table for direct access to any order by ID, and the full
// trade history.
//
// DATA STRUCTURE CHOICES
// -----------------------
//  - std::map<double, std::deque<std::shared_ptr<Order>>>
//      Price levels must stay sorted by price at all times (best bid / best
//      ask must always be retrievable in O(log N)), which is exactly what a
//      balanced BST-backed std::map gives us. Each price level stores its
//      resting orders in a std::deque, which gives O(1) push_back (new order
//      joins the back of the FIFO queue) and O(1) pop_front (the oldest order
//      at that price is matched first) -- this is what enforces time
//      priority within a price level.
//
//        * Buy side comparator is std::greater<double>, so the highest bid
//          is always buyLevels_.begin() -- O(1) best-bid access.
//        * Sell side uses the default std::less<double>, so the lowest ask
//          is always sellLevels_.begin() -- O(1) best-ask access.
//
//  - std::unordered_map<long long, std::shared_ptr<Order>>
//      Gives O(1) average-case lookup/cancel/modify of any order by ID,
//      independent of where it sits in the book.
//
//  - std::vector<Trade>
//      Trades are append-only and read sequentially for reporting, so a
//      contiguous vector is the most cache-friendly and simplest choice.
//
// COMPLEXITY SUMMARY
// -------------------
//  - addOrder:      O(log N) to touch a price level, plus O(K) to match
//                    against K resting orders that get fully consumed.
//  - cancelOrder:    O(1) average (hash lookup + status flag flip; the
//                    order is lazily unlinked from its deque the next time
//                    that price level is traversed).
//  - modifyOrder:    O(1) cancel + O(log N + K) re-insertion (same cost as
//                    addOrder, since a modify is implemented as cancel +
//                    replace, matching real exchange semantics where a
//                    price/quantity change loses time priority).
//  - searchOrder:    O(1) average.
// -----------------------------------------------------------------------------

// Snapshot of what happened immediately after submitting a new order.
struct AddOrderResult {
    long long orderId;
    int filledQuantity;
    int remainingQuantity;
    OrderStatus finalStatus;
};

class OrderBook {
public:
    OrderBook();

    // --- Core operations ---

    // Submits a new order, attempts to match it immediately against the
    // opposite side of the book, and rests any unfilled remainder.
    AddOrderResult addOrder(double price, int quantity, Side side);

    // Cancels an active order. O(1) average. Returns false if the order
    // does not exist or is no longer active.
    bool cancelOrder(long long orderId);

    // Modifies price and/or quantity of an existing order. Implemented as
    // cancel + re-submit, so the order loses its place in time priority --
    // this mirrors how real exchanges handle order modification.
    bool modifyOrder(long long orderId, double newPrice, int newQuantity);

    // --- Reporting ---
    void printOrderBook() const;
    void printTradeHistory() const;
    void printStatistics() const;

    // Prints details of a single order if found. Returns whether it exists.
    bool searchOrder(long long orderId) const;

    // --- Accessors ---
    std::size_t getTradeCount() const;
    std::size_t getActiveOrderCount() const;

private:
    // Bids: highest price first.
    std::map<double, std::deque<std::shared_ptr<Order>>, std::greater<double>> buyLevels_;
    // Asks: lowest price first.
    std::map<double, std::deque<std::shared_ptr<Order>>> sellLevels_;

    // O(1) average lookup of any order, regardless of side or price level.
    std::unordered_map<long long, std::shared_ptr<Order>> orderLookup_;

    std::vector<Trade> trades_;

    long long nextOrderId_;
    long long nextTradeId_;
    long long logicalClock_;   // Monotonically increasing counter; used both
                               // as a FIFO timestamp and as a trade timestamp.

    long long totalOrdersReceived_;
    long long totalOrdersCancelled_;
    long long totalVolumeTraded_;

    // --- Internal helpers ---

    // Attempts to match `incoming` against the resting book, generating
    // trades and mutating both `incoming` and any resting orders it fills.
    void matchOrder(const std::shared_ptr<Order>& incoming);

    // Inserts an order (with remaining quantity > 0) into the correct side
    // of the book.
    void addToBook(const std::shared_ptr<Order>& order);

    // Records a single execution and prints a trade log line.
    void recordTrade(const std::shared_ptr<Order>& buyOrder,
                      const std::shared_ptr<Order>& sellOrder,
                      double price,
                      int quantity);
};
