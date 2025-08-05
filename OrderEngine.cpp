#pragma once
#include "PriceHeap.cpp"
#include <memory>
#include <deque>
#include <random>
#include <thread>
#include <mutex>
#include <atomic>
#include <set>
#include <map>

// Order Status
enum class OrderStatus
{
    OPEN,
    FILLED,
    CANCELLED
};

// Order Types
enum class OrderType
{
    LIMIT,
    MARKET
};

// Order Sides
enum class OrderSide
{
    BID, 
    ASK
};

// Order Info
struct OrderInfo
{
    const OrderSide side;
    const OrderType type;
    OrderStatus status;
    double qty;
    double price;
    const unsigned int id;
    const std::time_t time;
    
    OrderInfo(const OrderSide _side, const OrderType _type, double _qty, double _price, const unsigned int _id) 
    : side(_side), type(_type), status(OrderStatus::OPEN), qty(_qty), price(_price), id(_id), time(std::time(nullptr))
    {
    }
};

// Aliases
using OrderLevel = std::deque<std::shared_ptr<OrderInfo>>;
using LevelMap = std::unordered_map<double, OrderLevel>;
using OrderMap = std::unordered_map<unsigned int, std::shared_ptr<OrderInfo>>;

// Order Matching Engine
class OrderEngine
{
public:
    OrderEngine() 
    : engine_running(true), book_updated(false), recent_order_id(0), next_order_id(1), AsksBook(true), BidsBook(false), vebose(true)
    {
        engine = std::thread(&OrderEngine::matching_engine, this);
    } 
   
    OrderEngine(bool _verbose) 
    : engine_running(true), book_updated(false), recent_order_id(0), next_order_id(1), AsksBook(true), BidsBook(false), vebose(_verbose)
    {
        engine = std::thread(&OrderEngine::matching_engine, this);
    } 
   
    OrderEngine(double _starting_price, bool _verbose) 
    : engine_running(true), book_updated(false), recent_order_id(0), next_order_id(1), 
    AsksBook(true), BidsBook(false), vebose(_verbose), starting_price(_starting_price)
    {
        engine = std::thread(&OrderEngine::matching_engine, this);
    } 
   
    ~OrderEngine()
    {
        std::unique_lock<std::mutex> lock(order_lock); 
        engine_running = false;
        book_updated = true;
        order_cv.notify_all();
        order_cv.wait(lock);
        if (engine.joinable()) 
            engine.join(); 
    }

    // POST: Place Limit Order
    unsigned int place_order(const OrderSide _side, const OrderType _type, double _qty, double _price = -1)
    {
        // Mutex
        std::unique_lock<std::mutex> lock(order_lock);
        
        // New Order ID
        const unsigned int _id = next_order_id++;
        
        if (_type == OrderType::MARKET) 
        {
            if (_side == OrderSide::ASK && !BidsBook.size())
                return 0; // No bids to match with
            if (_side == OrderSide::BID && !AsksBook.size())
                return 0; // No asks to match with
        }

        // New Order
        std::shared_ptr<OrderInfo> new_order;
        switch (_type)
        {
            case OrderType::LIMIT: // Limit Order
                {
                    if (_side == OrderSide::ASK && _price < BidsBook.peek())
                        _price = BidsBook.peek(); // Adjust price to best bid
                    else if (_side == OrderSide::BID && _price > AsksBook.peek())
                        _price = AsksBook.peek(); // Adjust price to best ask
                    new_order = std::make_shared<OrderInfo>(_side, OrderType::LIMIT, _qty, _price, _id);
                    break;
                }

           case OrderType::MARKET: // Market Order
                {
                    // If Market Order, get best price from opposite book
                    double _price = _side == OrderSide::ASK ? BidsBook.peek() : AsksBook.peek();
                    new_order = std::make_shared<OrderInfo>(_side, OrderType::MARKET, _qty, _price, _id);
                    break;
                }
                
            default:
                return 0; // Invalid Order Type
        }
            
        switch (_side)
        {
            case OrderSide::ASK:
                {
                    // Create new ask price level if no price level
                    if (AsksBook.find(_price) == -1)
                    {
                        AsksBook.push(_price);
                        AskLevels[_price] = OrderLevel();
                    }
                    AskLevels[_price].push_back(new_order);
                    OrderTable[_id] = new_order;
                    break;
                }
            
            case OrderSide::BID:
                {
                    // Create new bid price level if no price level
                    if (BidsBook.find(_price) == -1)
                    {
                        BidsBook.push(_price);
                        BidLevels[_price] = OrderLevel();
                    }
                    BidLevels[_price].push_back(new_order);
                    OrderTable[_id] = new_order;
                    break;
                }
            
            default:
                return 0; // Invalid Order Side
        }

        // Notifiy Open
        notify_open(_id);
        recent_order_id = _id;

        // Notify Matching Engine
        book_updated = true;
        order_cv.notify_all();
        order_cv.wait(lock, [this]{ 
            return !book_updated; 
        }); // Wait for matching engine
        return _id; // Return Order ID
    }

    // POST: Cancel Order
    bool cancel_order(const unsigned int _id)
    {
        // Mutex
        std::unique_lock<std::mutex> lock(order_lock);
 
        if (OrderTable.find(_id) == OrderTable.end()) 
            return false; // Order does not exist;
        
        std::shared_ptr<OrderInfo>& order = OrderTable[_id];
        if (order->status != OrderStatus::OPEN || order->type != OrderType::LIMIT)
            return false; // Order is not open or not a limit order

        if (order->side != OrderSide::BID && order->side != OrderSide::ASK)
            return false; // Invalid Order Side

        // Get Order Level based on Order Side
        OrderLevel& order_level = (order->side == OrderSide::BID) ?
        BidLevels[order->price] : AskLevels[order->price];

        // Iterate thruogh order level filtering out ORDER ID
        OrderLevel new_level;
        for (auto& cur_order : order_level)
        {
            if (cur_order->id != _id) new_level.push_back(cur_order);
        }
        order_level = std::move(new_level);

        // If Order Level is empty pop from Book and erase Order Level
        if (order_level.empty())
        {
            switch(order->side)
            {
                case OrderSide::BID:
                    BidsBook.pop(BidsBook.find(order->price));
                    BidLevels.erase(order->price);
                    break;
                case OrderSide::ASK:
                    AsksBook.pop(AsksBook.find(order->price));
                    AskLevels.erase(order->price);
                    break;
                default:
                    return false; // Invalid Order Side
            }
        }

        // Notify Cancel
        notify_cancel(_id);

        // Notify Matching Engine
        book_updated = true;
        order_cv.notify_all();
        order_cv.wait(lock, [this]() { return !book_updated; });
        return true; // Order successfully canceled
    }

    // PATCH: Edit Order
    unsigned int edit_order(const unsigned int _id, const OrderSide _side, const double _qty, const double _price)
    {
        if (!cancel_order(_id))
            return 0; // If cancel failed
        return place_order(_side, OrderType::LIMIT, _qty, _price);
    }

    // GET: Get Order
    std::shared_ptr<OrderInfo> get_order(const unsigned int& _id) const
    { 
            const auto& order = OrderTable.find(_id);
            if (order == OrderTable.end())
                return nullptr; // Return nullptr if order not found
            return order->second; 
    }
    
    // GET: Average Price
    double get_price() const 
    {
        if (!(AsksBook.size() || BidsBook.size())) 
            return starting_price; // If no orders, return starting price
        return (AsksBook.peek() + BidsBook.peek()) / 2; // Average of best ask and bid
    }

    // GET: Best Ask
    double get_best_ask() const 
    {
        if (!AsksBook.size()) return -1;
        return AsksBook.peek();
    }

    // GET: Best Bid
    double get_best_bid() const 
    {
        if (!BidsBook.size()) return -1;
        return BidsBook.peek();
    }

    // New: Get orders by status
    std::vector<std::shared_ptr<OrderInfo>> get_orders_by_status(OrderStatus status) const {
        std::vector<std::shared_ptr<OrderInfo>> result;
        for (const auto& [id, order] : OrderTable) {
            if (order->status == status)
                result.push_back(order);
        }
        return result;
    }
    
    // TODO: (GET: Maket Depth)
    std::vector<std::pair<double, std::size_t>> get_market_depth(OrderSide _side, std::size_t _depth = 10) const;

private:
    // Order Book
    PriceHeap AsksBook; // Asks Order Book
    PriceHeap BidsBook; // Bids Order Book
    LevelMap AskLevels; // Asks Price Levels
    LevelMap BidLevels; // Bids Price Levels
    OrderMap OrderTable; // Map to all active orders
    unsigned int recent_order_id; // New Orders ID
    unsigned int next_order_id; // Next Order ID

    // Concurreny
    std::thread engine;
    std::mutex order_lock;
    std::condition_variable order_cv;
    std::atomic<bool> engine_running;
    bool book_updated;

    bool vebose; // Verbose Mode
    double starting_price; // Starting Price

    // Matching Engine
    void matching_engine()
    {
        while (engine_running.load())
        {
            // Sleep Lock
            std::unique_lock<std::mutex> lock(order_lock);
            order_cv.wait(lock, [this]{ 
                    return  book_updated; 
            });
            
            auto is_recent_order = OrderTable.find(recent_order_id) != OrderTable.end();
            auto is_books = AsksBook.size() && BidsBook.size();

            if (engine_running && is_recent_order && is_books)
            {
                // Get Recent Order
                std::shared_ptr<OrderInfo> recent_order = OrderTable[recent_order_id];
                while (recent_order && recent_order->status == OrderStatus::OPEN && recent_order->qty)
                {
                    // Get best asks and bids
                    const double& best_asks_price = AsksBook.peek();
                    const double& best_bids_price = BidsBook.peek();
                    if (AskLevels.find(best_asks_price) == AskLevels.end() ||
                        BidLevels.find(best_bids_price) == BidLevels.end())
                        break; // No best price level to match with
                        
                    // Get price level for best asks and bids
                    OrderLevel& best_level_asks = AskLevels[best_asks_price];
                    OrderLevel& best_level_bids = BidLevels[best_bids_price];
                    if (best_level_asks.empty() || best_level_bids.empty())
                        break; // No best level to match with
                    
                    // Get OrderInfo for best ask and bid
                    std::shared_ptr<OrderInfo>& best_ask = best_level_asks.front();
                    std::shared_ptr<OrderInfo>& best_bid = best_level_bids.front();
                    
                    // Break If you can't trade
                    const bool can_trade_asks = recent_order->side == OrderSide::ASK && 
                    !best_level_bids.empty() && best_bid->price >= recent_order->price;
                    const bool can_trade_bids = recent_order->side == OrderSide::BID && 
                    !best_level_asks.empty() && best_ask->price <= recent_order->price;
                    if (!(can_trade_asks || can_trade_bids)) 
                        break; // No match possible

                    switch (recent_order->side)
                    {
                        case OrderSide::ASK:
                            {
                                matching(recent_order, best_bid, best_level_asks, best_level_bids);
                                break;
                            }
                        
                        case OrderSide::BID:
                            {
                                matching(best_ask, recent_order, best_level_asks, best_level_bids);
                                break;
                            }
                    }                
                }
            } 
            
            // Notify Order Engine
            book_updated = false;
            order_cv.notify_all();
        }
    }

    // Match Orders
    void matching(std::shared_ptr<OrderInfo>& best_ask, std::shared_ptr<OrderInfo>& best_bid, OrderLevel& best_level_asks, OrderLevel& best_level_bids)
    {   
        // Get qty filled and apply difference
        const double qty_filled = std::min(best_ask->qty, best_bid->qty);
        best_ask->qty -= qty_filled;
        best_bid->qty -= qty_filled;
        
        notify_fill(best_ask->id, qty_filled);
        notify_fill(best_bid->id, qty_filled);

        // If ask qty is 0 then notify fill and erase
        if (!best_ask->qty)
        {
            best_level_asks.pop_front();
            // If ask level is now empty then erase level
            if (!best_level_asks.size())
            {
                AsksBook.pop();
                AskLevels.erase(best_ask->price);
            }
        }

        // If ask qty is 0 then notify fill and erase
        if (!best_bid->qty)
        {
            best_level_bids.pop_front();
            // If ask level is now empty then erase level
            if (!best_level_bids.size())
            {
                BidsBook.pop();
                BidLevels.erase(best_bid->price);
            }
        }
    }

    // Notify of what Orders are open
    void notify_open(const unsigned int& _id)
    {
        if (OrderTable.find(_id) == OrderTable.end()) 
            throw std::runtime_error("Could Not Find Open Order");

        const std::shared_ptr<OrderInfo>& order = OrderTable[_id];
        const std::string _side = order->side == OrderSide::BID ? "BUY" : "SELL";
        const std::string _type = order->type == OrderType::LIMIT ? "LIMIT" : "MARKET";
        order->status = OrderStatus::OPEN; // Update Order Status
        
        // Notification
        if (!vebose) return; // If not verbose, do not notify
        std::cout << "[OPEN] | " << "TYPE: " << _type << " | ID: " << order->id << " | SIDE: " << _side << 
        " | QTY: " << order->qty << " | PRICE: " << order->price << " | TIME: "  << order->time << std::endl;
    }

    // Notify of what Orders were filled
    void notify_fill(const unsigned int& _id, const double& qty_filled)
    {
        if (OrderTable.find(_id) == OrderTable.end()) 
            throw std::runtime_error("Could Not Find Filled Order");

        const std::shared_ptr<OrderInfo>& order = OrderTable[_id];
        const std::string _side = order->side == OrderSide::BID ? "BUY" : "SELL";
        const std::string _type = order->type == OrderType::LIMIT ? "LIMIT" : "MARKET";
        const std::string status = !order->qty ? "[FILLED]" : "[PARTIALLY FILLED]";

        if (!order->qty)
            order->status = OrderStatus::FILLED; // Update Order Status

        // Time Order was Filled
        const std::time_t& _time = time(0);
        
        // Notification
        if (!vebose) return; // If not verbose, do not notify
        std::cout << status << " | " << "TYPE: " << _type << " | ID: " << order->id << " | SIDE: " << _side << 
        " | QTY: " << qty_filled << " | PRICE: " << order->price << " | TIME: "  << _time << std::endl;
    }

    // Notify of what Orders were canceled
    void notify_cancel(const unsigned int& _id)
    {
        if (OrderTable.find(_id) == OrderTable.end()) 
            throw std::runtime_error("Could Not Find Cancelled Order");

        const std::shared_ptr<OrderInfo>& order = OrderTable[_id];
        const std::string& _side = order->side == OrderSide::BID ? "BUY" : "SELL";
        const std::string _type = order->type == OrderType::LIMIT ? "LIMIT" : "MARKET";
        order->status = OrderStatus::CANCELLED; // Update Order Status

        // Time Order was Canceled
        const std::time_t& _time = time(0);
        
        // Notification
        if (!vebose) return; // If not verbose, do not notify
        std::cout << "[CANCELED] | " << "TYPE: " << _type << " | ID: " << order->id << " | SIDE: " << _side << 
        " | QTY: " << order->qty << " | PRICE: " << order->price << " | TIME: "  << _time << std::endl;
    }
};
