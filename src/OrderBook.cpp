#include "OrderBook.h"

#include <algorithm>
#include <iomanip>
#include <iostream>
#include <sstream>

namespace {

// Small formatting helpers kept local to this translation unit.

std::string sideToString(Side side) {
    return side == Side::BUY ? "BUY" : "SELL";
}

std::string statusToString(OrderStatus status) {
    switch (status) {
        case OrderStatus::ACTIVE:           return "ACTIVE";
        case OrderStatus::PARTIALLY_FILLED: return "PARTIALLY_FILLED";
        case OrderStatus::FILLED:           return "FILLED";
        case OrderStatus::CANCELLED:        return "CANCELLED";
    }
    return "UNKNOWN";
}

} // namespace

OrderBook::OrderBook()
    : nextOrderId_(1),
      nextTradeId_(1),
      logicalClock_(0),
      totalOrdersReceived_(0),
      totalOrdersCancelled_(0),
      totalVolumeTraded_(0) {}

AddOrderResult OrderBook::addOrder(double price, int quantity, Side side) {
    const long long id = nextOrderId_++;
    const long long timestamp = logicalClock_++;

    auto order = std::make_shared<Order>(id, price, quantity, side, timestamp);
    orderLookup_[id] = order;
    ++totalOrdersReceived_;

    std::cout << "New " << sideToString(side) << " Order | ID: " << id
              << " | Price: " << price << " | Qty: " << quantity << '\n';

    matchOrder(order);

    if (order->getStatus() == OrderStatus::FILLED) {
        std::cout << "  Order fully filled | ID: " << id << '\n';
    } else if (order->getRemainingQuantity() > 0) {
        addToBook(order);
        std::cout << "  Resting in book | ID: " << id
                  << " | Remaining Qty: " << order->getRemainingQuantity() << '\n';
    }

    return AddOrderResult{id,
                           quantity - order->getRemainingQuantity(),
                           order->getRemainingQuantity(),
                           order->getStatus()};
}

void OrderBook::matchOrder(const std::shared_ptr<Order>& incoming) {
    if (incoming->getSide() == Side::BUY) {
        // A BUY order matches against the lowest-priced resting SELL orders,
        // as long as that ask price is not higher than what the buyer is
        // willing to pay.
        while (incoming->getRemainingQuantity() > 0 && !sellLevels_.empty()) {
            auto bestAskIt = sellLevels_.begin();
            const double bestAskPrice = bestAskIt->first;

            if (bestAskPrice > incoming->getPrice()) {
                break; // Cheapest ask is still too expensive; no match possible.
            }

            auto& fifoQueue = bestAskIt->second;

            while (!fifoQueue.empty() && incoming->getRemainingQuantity() > 0) {
                auto resting = fifoQueue.front();

                if (!resting->isActive()) {
                    // Lazily drop cancelled / already-filled orders we
                    // encounter at the front of the queue.
                    fifoQueue.pop_front();
                    continue;
                }

                const int tradedQty = std::min(incoming->getRemainingQuantity(),
                                                resting->getRemainingQuantity());

                recordTrade(incoming, resting, resting->getPrice(), tradedQty);

                incoming->fill(tradedQty);
                resting->fill(tradedQty);

                if (resting->getRemainingQuantity() == 0) {
                    fifoQueue.pop_front();
                }
            }

            if (fifoQueue.empty()) {
                sellLevels_.erase(bestAskIt);
            }
        }
    } else {
        // A SELL order matches against the highest-priced resting BUY
        // orders, as long as that bid price is not lower than what the
        // seller is willing to accept.
        while (incoming->getRemainingQuantity() > 0 && !buyLevels_.empty()) {
            auto bestBidIt = buyLevels_.begin();
            const double bestBidPrice = bestBidIt->first;

            if (bestBidPrice < incoming->getPrice()) {
                break; // Highest bid still doesn't meet the seller's price.
            }

            auto& fifoQueue = bestBidIt->second;

            while (!fifoQueue.empty() && incoming->getRemainingQuantity() > 0) {
                auto resting = fifoQueue.front();

                if (!resting->isActive()) {
                    fifoQueue.pop_front();
                    continue;
                }

                const int tradedQty = std::min(incoming->getRemainingQuantity(),
                                                resting->getRemainingQuantity());

                recordTrade(resting, incoming, resting->getPrice(), tradedQty);

                incoming->fill(tradedQty);
                resting->fill(tradedQty);

                if (resting->getRemainingQuantity() == 0) {
                    fifoQueue.pop_front();
                }
            }

            if (fifoQueue.empty()) {
                buyLevels_.erase(bestBidIt);
            }
        }
    }
}

void OrderBook::addToBook(const std::shared_ptr<Order>& order) {
    if (order->getSide() == Side::BUY) {
        buyLevels_[order->getPrice()].push_back(order);
    } else {
        sellLevels_[order->getPrice()].push_back(order);
    }
}

void OrderBook::recordTrade(const std::shared_ptr<Order>& buyOrder,
                             const std::shared_ptr<Order>& sellOrder,
                             double price,
                             int quantity) {
    trades_.emplace_back(nextTradeId_++, buyOrder->getId(), sellOrder->getId(),
                          price, quantity, logicalClock_++);
    totalVolumeTraded_ += quantity;

    std::cout << "  >> Trade Executed | Buy#" << buyOrder->getId()
              << " x Sell#" << sellOrder->getId()
              << " | Price: " << price
              << " | Qty: " << quantity << '\n';
}

bool OrderBook::cancelOrder(long long orderId) {
    auto it = orderLookup_.find(orderId);
    if (it == orderLookup_.end()) {
        std::cout << "Cancel Failed | Order ID " << orderId << " not found\n";
        return false;
    }

    auto& order = it->second;
    if (!order->isActive()) {
        std::cout << "Cancel Failed | Order ID " << orderId << " is already "
                  << statusToString(order->getStatus()) << '\n';
        return false;
    }

    // O(1): flip the status flag. The order is physically unlinked from its
    // price-level deque lazily, the next time that level is traversed by
    // matchOrder() or a print/report routine.
    order->setStatus(OrderStatus::CANCELLED);
    ++totalOrdersCancelled_;

    std::cout << "Order Cancelled | ID: " << orderId << '\n';
    return true;
}

bool OrderBook::modifyOrder(long long orderId, double newPrice, int newQuantity) {
    auto it = orderLookup_.find(orderId);
    if (it == orderLookup_.end() || !it->second->isActive()) {
        std::cout << "Modify Failed | Order ID " << orderId << " not found or inactive\n";
        return false;
    }

    const Side side = it->second->getSide();

    // Cancel the original order (loses time priority) and submit a fresh
    // order with the new price/quantity, exactly as most real exchanges do.
    it->second->setStatus(OrderStatus::CANCELLED);
    ++totalOrdersCancelled_;

    std::cout << "Order Modified | Old ID: " << orderId
              << " cancelled -> submitting replacement\n";

    addOrder(newPrice, newQuantity, side);
    return true;
}

void OrderBook::printOrderBook() const {
    std::cout << "\n===================== ORDER BOOK =====================\n";
    std::cout << std::left
              << std::setw(8) << "SIDE"
              << std::setw(10) << "PRICE"
              << std::setw(8) << "QTY"
              << "ORDERS (FIFO: oldest first)\n";

    std::cout << "--- ASKS (lowest price first) ---\n";
    for (const auto& [price, fifoQueue] : sellLevels_) {
        int levelQty = 0;
        std::ostringstream orderList;
        for (const auto& order : fifoQueue) {
            if (!order->isActive()) continue;
            if (levelQty > 0) orderList << ", ";
            orderList << "#" << order->getId() << "(" << order->getRemainingQuantity() << ")";
            levelQty += order->getRemainingQuantity();
        }
        if (levelQty == 0) continue;
        std::cout << std::left << std::setw(8) << "SELL"
                   << std::setw(10) << price
                   << std::setw(8) << levelQty
                   << orderList.str() << '\n';
    }

    std::cout << "--- BIDS (highest price first) ---\n";
    for (const auto& [price, fifoQueue] : buyLevels_) {
        int levelQty = 0;
        std::ostringstream orderList;
        for (const auto& order : fifoQueue) {
            if (!order->isActive()) continue;
            if (levelQty > 0) orderList << ", ";
            orderList << "#" << order->getId() << "(" << order->getRemainingQuantity() << ")";
            levelQty += order->getRemainingQuantity();
        }
        if (levelQty == 0) continue;
        std::cout << std::left << std::setw(8) << "BUY"
                   << std::setw(10) << price
                   << std::setw(8) << levelQty
                   << orderList.str() << '\n';
    }
    std::cout << "========================================================\n";
}

void OrderBook::printTradeHistory() const {
    std::cout << "\n===================== TRADE HISTORY ===================\n";
    if (trades_.empty()) {
        std::cout << "(no trades executed yet)\n";
    } else {
        std::cout << std::left
                  << std::setw(10) << "TRADE#"
                  << std::setw(10) << "BUY_ID"
                  << std::setw(10) << "SELL_ID"
                  << std::setw(10) << "PRICE"
                  << std::setw(8) << "QTY" << '\n';
        for (const auto& trade : trades_) {
            std::cout << std::left
                      << std::setw(10) << trade.tradeId
                      << std::setw(10) << trade.buyOrderId
                      << std::setw(10) << trade.sellOrderId
                      << std::setw(10) << trade.price
                      << std::setw(8) << trade.quantity << '\n';
        }
    }
    std::cout << "========================================================\n";
}

bool OrderBook::searchOrder(long long orderId) const {
    auto it = orderLookup_.find(orderId);
    if (it == orderLookup_.end()) {
        std::cout << "Search | Order ID " << orderId << " not found\n";
        return false;
    }

    const auto& order = it->second;
    std::cout << "Search | Order ID " << orderId
              << " | Side: " << sideToString(order->getSide())
              << " | Price: " << order->getPrice()
              << " | Original Qty: " << order->getQuantity()
              << " | Remaining Qty: " << order->getRemainingQuantity()
              << " | Status: " << statusToString(order->getStatus())
              << " | Timestamp: " << order->getTimestamp() << '\n';
    return true;
}

std::size_t OrderBook::getTradeCount() const {
    return trades_.size();
}

std::size_t OrderBook::getActiveOrderCount() const {
    std::size_t count = 0;
    for (const auto& [id, order] : orderLookup_) {
        if (order->isActive()) ++count;
    }
    return count;
}

void OrderBook::printStatistics() const {
    std::cout << "\n===================== ENGINE STATISTICS ===============\n";
    std::cout << "Total Orders Received : " << totalOrdersReceived_ << '\n';
    std::cout << "Total Orders Cancelled: " << totalOrdersCancelled_ << '\n';
    std::cout << "Currently Active Orders: " << getActiveOrderCount() << '\n';
    std::cout << "Total Trades Executed  : " << trades_.size() << '\n';
    std::cout << "Total Volume Traded    : " << totalVolumeTraded_ << '\n';

    if (!buyLevels_.empty()) {
        std::cout << "Best Bid                : " << buyLevels_.begin()->first << '\n';
    } else {
        std::cout << "Best Bid                : (none)\n";
    }

    if (!sellLevels_.empty()) {
        std::cout << "Best Ask                : " << sellLevels_.begin()->first << '\n';
    } else {
        std::cout << "Best Ask                : (none)\n";
    }
    std::cout << "========================================================\n";
}
