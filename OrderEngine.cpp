#include <iostream>
#include <memory>
#include <vector>
#include <deque>
#include <random>
#include <thread>
#include <mutex>
#include <atomic>
#include<ctime>
#include <map>

// Order Types
enum side_type
{
    BID, 
    ASK
};

// Order
struct OrderInfo
{
    const side_type side;
    double qty;
    double price;
    const unsigned int id;

    OrderInfo(const side_type _side, double _qty, double _price, const unsigned int _id) 
    : qty(_qty), price(_price), side(_side), id(_id)
    {
    } 
};

// Order Level 
using OrderLevel = std::deque<std::shared_ptr<OrderInfo>>;

// Order Book for Asks
class AsksBook
{
public:
    // Push level
    void push(double level)
    {
        asks.push(level);
    }

    // Pop best ask
    double pop()
    {
        try
        {            
            if (!size()) throw std::runtime_error("Asks Book is Empty");
            double _level = asks.top();
            asks.pop();
            return _level;
        } 
        catch(const std::exception& e)
        {
            std::cerr << e.what() << '\n';
            return -1;
        }
    }

    // Peek best ask
    double peek() const
    { 
        try
        {
            if (!size()) throw std::runtime_error("Asks Book is Empty");
            return asks.top();
        } 
        catch(std::exception &error)
        {
            std::cerr << error.what() << std::endl;
            return -1;
        }
    }

    // Size
    int size() const{ return asks.size(); }

private:
    // Asks Heap (MIN-HEAP)
    std::priority_queue<double, std::vector<double>, std::greater<double>> asks;
};

// Order Book for Bids
class BidsBook
{
public:
    // Push level
    void push(double level)
    {
        bids.push(level);
    }

    // Pop best bid
    double pop()
    {
        try
        {            
            if (!size()) throw std::runtime_error("Bids Book is Empty");
            double _level = bids.top();
            bids.pop();
            return _level;
        } 
        catch(const std::exception& e)
        {
            std::cerr << e.what() << '\n';
            return -1;
        }
    }

    // Peek best bid
    double peek() const
    { 
        try
        {
            if (!size()) throw std::runtime_error("Bids Book is Empty");
            return bids.top();
        } 
        catch(std::exception &error)
        {
            std::cerr << error.what() << std::endl;
            return -1;
        }
    }

    // Size
    int size() const{ return bids.size(); }

private:
    // Bids Heap (MAX-HEAP)
    std::priority_queue<double, std::vector<double>, std::less<double>> bids;
};

// Order API
class OrderEngine
{
public:
    OrderEngine() 
    : engine_running(true), book_updated(false), recent_order_id(0), gen(std::random_device{}()), dist(0, UINT32_MAX)
    {
        engine = std::thread(&OrderEngine::matching_engine, this);
    } 
    ~OrderEngine()
    {
        engine_running = false;
        order_cv.notify_one();
        if (engine.joinable()) engine.join(); 
    }

    const unsigned int place_order(const side_type _side, double _qty, double _price)
    {
        // Mutex
        std::unique_lock<std::mutex> lock(order_lock);
        
        // Random ID Generation
        const unsigned int _id = dist(gen);

        // New Order
        std::shared_ptr<OrderInfo> new_order = std::make_shared<OrderInfo>(_side, _qty, _price, _id);

        switch (_side)
        {
        case ASK:
            {
                // Create price level if no price level
                if (price_level_asks.find(_price) == price_level_asks.end())
                {
                    OrderLevel new_level;
                    AsksBook.push(_price);
                    price_level_asks[_price] = new_level;
                }
                price_level_asks[_price].push_back(new_order);
                order_table[_id] = new_order;
                break;
            }
        
        case BID:
            {
                // Create price level if no price level
                if (price_level_bids.find(_price) == price_level_bids.end())
                {
                    OrderLevel new_level;
                    BidsBook.push(_price);
                    price_level_bids[_price] = new_level;
                }
                price_level_bids[_price].push_back(new_order);
                order_table[_id] = new_order;
                break;
            }
        
        default:
            break;
        }

        recent_order_id = _id;
        book_updated.store(true);
        order_processed.store(false);
        order_cv.notify_one(); // Wake Matching Engine
        order_cv.wait(lock, [this]{ return !book_updated.load() || order_processed.load(); }); // Wait for matching engine to process the order
        return _id;
    }

private:
    // Order Book
    AsksBook AsksBook; // Asks Order Book
    BidsBook BidsBook; // Bids Order Book
    std::map<double, OrderLevel> price_level_asks; // Asks Price Levels
    std::map<double, OrderLevel> price_level_bids; // Bids Price Levels
    std::map<unsigned int, std::shared_ptr<OrderInfo>> order_table; // Map to all active orders
    unsigned int recent_order_id; // New Orders ID

    // Concurreny
    std::thread engine;
    std::mutex order_lock;
    std::condition_variable order_cv;
    std::atomic<bool> book_updated;
    std::atomic<bool> order_processed;
    std::atomic<bool> engine_running;

    // Random
    std::mt19937_64 gen;
    std::uniform_int_distribution<unsigned int> dist;

    // Filled Orders
    std::vector<std::tuple<const unsigned int, const std::string, const double, const double>> filled_orders;
    
    // Matching Engine
    void matching_engine()
    {
        while (engine_running.load())
        {
            try
            {
                // Sleep Lock
                std::unique_lock<std::mutex> lock(order_lock);
                order_cv.wait(lock, [this]{ return book_updated.load() || !engine_running || (AsksBook.size() && BidsBook.size()); });
                
                // If Asks or Bids is Empty Continue
                if (!(AsksBook.size() && BidsBook.size())) throw std::runtime_error("Asks or Bids is empty");
                
                // Get best asks and bids
                const double best_asks_price = AsksBook.peek();
                const double best_bids_price = BidsBook.peek();
                // Get price level for best asks and bids
                OrderLevel &best_level_asks = price_level_asks[best_asks_price];
                OrderLevel &best_level_bids = price_level_bids[best_bids_price];
                
                // Get Recent Order
                std::shared_ptr<OrderInfo> recent_order = order_table[recent_order_id];
                while (recent_order->qty)
                {
                    // Break If you can't trade
                    bool can_trade_asks = recent_order->side == ASK && !best_level_bids.empty() && best_level_bids.front()->price >= recent_order->price;
                    bool can_trade_bids = recent_order->side == BID && !best_level_asks.empty() && best_level_asks.front()->price <= recent_order->price;
                    if (!(can_trade_asks || can_trade_bids)) break;
                    
                    // Get OrderInfo for best ask and bid
                    std::shared_ptr<OrderInfo> best_ask = best_level_asks.front();
                    std::shared_ptr<OrderInfo> best_bid = best_level_bids.front();

                    switch (recent_order->side)
                    {
                    case ASK:
                        {
                            recent_order->price = recent_order->price > best_bid->price ? recent_order->price : best_bid->price;
                            matching(recent_order, best_bid, best_level_asks, best_level_bids);
                            break;
                        }
                    
                    case BID:
                        {
                            recent_order->price = recent_order->price < best_ask->price ? recent_order->price : best_ask->price;
                            matching(best_ask, recent_order, best_level_asks, best_level_bids);
                            break;
                        }
    
                    default:
                        break;
                    }                
                }
            } catch (std::exception &error)
            {
                std::cerr << error.what() << std::endl;
            }

            book_updated.store(false);
            order_processed.store(true);
            order_cv.notify_one(); // Notify that the order has been processed
        }
    }

    // Match Orders
    void matching(std::shared_ptr<OrderInfo> best_ask, std::shared_ptr<OrderInfo> best_bid, OrderLevel &best_level_asks, OrderLevel &best_level_bids)
    {
        if (best_ask->qty > best_bid->qty)
        {
            double qty_filled = best_bid->qty;
            best_ask->qty -= best_bid->qty;
            best_bid->qty = 0;

            best_level_bids.pop_front();
            notify(best_bid->id, best_bid->qty);
        } 
        else if (best_ask->qty < best_bid->qty)
        {
            double qty_filled = best_ask->qty;
            best_bid->qty -= best_ask->qty;
            best_ask->qty = 0;

            best_level_asks.pop_front();
            notify(best_ask->id, qty_filled);
        } 
        else
        {   
            double qty_filled = best_ask->qty;
            best_ask->qty = 0;
            best_bid->qty = 0;

            best_level_bids.pop_front();
            notify(best_ask->id, qty_filled);
            best_level_asks.pop_front();
            notify(best_bid->id, qty_filled);
        }
        
        // If best asks level is empty then pop level and erase price map
        if (!best_level_asks.size())
        {
            AsksBook.pop();
            price_level_asks.erase(best_ask->price);
        }
        // If best bids level is empty then pop level and erase price map
        if (!best_level_bids.size())
        {
            BidsBook.pop();
            price_level_bids.erase(best_bid->price);
        }
    }

    // Notify of what Orders were filled
    void notify(const unsigned int _id, const double qty_filled)
    {
        std::shared_ptr<OrderInfo> order = order_table[_id];
        std::string side = order->side == BID ? "BUY" : "SELL";
        std::cout << "[FILLED] | " << "ID: " << order->id << " | SIDE: " << side << " | QTY: " << qty_filled << " | PRICE: "
        << order->price << std::endl;
        std::tuple<const unsigned int, std::string, const double, const double> filled_order(
            order->id, side, qty_filled, order->price
        );
        filled_orders.push_back(filled_order);
        order_table.erase(_id);
    }
};


int main()
{    
    OrderEngine OrderEngine;
    OrderEngine.place_order(BID, 10, 100);
    OrderEngine.place_order(ASK, 5, 99);
    OrderEngine.place_order(ASK, 5, 100);
    OrderEngine.place_order(BID, 5, 101);
}