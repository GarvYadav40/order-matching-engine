#include <iostream>
#include <string>
#include "OrderBook.h"

// -----------------------------------------------------------------------------
// main.cpp
//
// Walks through a realistic sequence of exchange activity to demonstrate
// every feature of the engine: order entry, automatic matching, partial
// fills, price-time priority, cancellation, modification, order lookup,
// and reporting.
// -----------------------------------------------------------------------------

void section(const std::string& title) {
    std::cout << "\n########## " << title << " ##########\n";
}

int main() {
    OrderBook engine;

    // -------------------------------------------------------------------
    section("1. Building up the book (no crosses yet)");
    // -------------------------------------------------------------------
    engine.addOrder(99.5, 15, Side::BUY);   // ID 1
    engine.addOrder(99.0, 10, Side::BUY);   // ID 2
    engine.addOrder(99.5, 5, Side::BUY);    // ID 3 -- same price as ID 1, joins behind it (FIFO)
    engine.addOrder(101.0, 8, Side::SELL);  // ID 4
    engine.addOrder(102.0, 12, Side::SELL); // ID 5

    engine.printOrderBook();

    // -------------------------------------------------------------------
    section("2. Partial fill example (BUY 100 x10 vs SELL 100 x3)");
    // -------------------------------------------------------------------
    // Matches the exact scenario from the spec: a resting sell for 3 should
    // leave the incoming buy with a remaining quantity of 7.
    engine.addOrder(100.0, 3, Side::SELL);  // ID 6 -- rests, nothing to match yet
    engine.addOrder(100.0, 10, Side::BUY);  // ID 7 -- should fill 3, rest 7

    engine.printOrderBook();
    engine.printTradeHistory();

    // -------------------------------------------------------------------
    section("3. Price-time priority: BUY sweeps through the ask side");
    // -------------------------------------------------------------------
    // Best ask is 101.0 (ID 4, qty 8). A buy at 101.5 for 20 should:
    //   - fully fill ID 4 (8 units)
    //   - then fully fill ID 5 at 102.0 (12 units)
    //   - leaving 0 remaining, fully filled overall
    engine.addOrder(101.5, 20, Side::BUY); // ID 8

    engine.printOrderBook();
    engine.printTradeHistory();

    // -------------------------------------------------------------------
    section("4. Cancel Order");
    // -------------------------------------------------------------------
    engine.cancelOrder(2);   // Cancel resting BUY 99.0 x10
    engine.cancelOrder(999); // Non-existent order -> should fail gracefully
    engine.printOrderBook();

    // -------------------------------------------------------------------
    section("5. Modify Order");
    // -------------------------------------------------------------------
    // Order 3 (BUY 99.5 x5) is bumped up to 100.5 x5. The best ask is still
    // 102.0, so the modified order does not cross -- it simply re-rests at
    // its new price, now ahead of order 7's level in the bid ladder.
    engine.modifyOrder(3, 100.5, 5);

    engine.printOrderBook();
    engine.printTradeHistory();

    // -------------------------------------------------------------------
    section("6. Search Order by ID");
    // -------------------------------------------------------------------
    engine.searchOrder(1);
    engine.searchOrder(7);
    engine.searchOrder(3);   // Original ID 3 was cancelled by the modify above
    engine.searchOrder(42);  // Does not exist

    // -------------------------------------------------------------------
    section("7. Engine Statistics");
    // -------------------------------------------------------------------
    engine.printStatistics();

    return 0;
}
