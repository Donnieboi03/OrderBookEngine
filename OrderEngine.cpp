#include "PriceHeap.cpp"
#include <memory>
#include <deque>
#include <random>
#include <thread>
#include <mutex>
#include <atomic>
#include <set>
#include <map>

// Order Types
enum side_type
{
    BID, 
    ASK
};

// Order Info
struct OrderInfo
{
    const side_type side;
    double initial_qty;
    double qty;
    double price;
    const unsigned int id;
    const std::time_t time;
    
    OrderInfo(const side_type _side, double _qty, double _price, const unsigned int _id, const std::time_t _time) 
    : initial_qty(_qty), qty(_qty), price(_price), side(_side), id(_id), time(_time)
    {
    }
};

// Aliases
using OrderLevel = std::deque<std::shared_ptr<OrderInfo>>;
using LevelMap = std::unordered_map<double, OrderLevel>;
using OrderMap = std::unordered_map<unsigned int, std::shared_ptr<OrderInfo>>;
using OrderInfoList = std::set<std::tuple<std::time_t, unsigned int, std::string, double, double>>;

// Order API
class OrderEngine
{
public:
    OrderEngine() 
    : engine_running(true), book_updated(false), recent_order_id(0), gen(std::random_device{}()), dist(1, UINT32_MAX),
    AsksBook(true), BidsBook(false)
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
        if (engine.joinable()) engine.join(); 
    }

    // POST: Place Order
    unsigned int place_order(const side_type& _side, const double& _qty, double _price)
    {
        // Mutex
        std::unique_lock<std::mutex> lock(order_lock);
        try{
            if ((_side != BID) && (_side != ASK)) 
                throw std::runtime_error("Invalid Side");
            if (!_qty) 
                throw std::runtime_error("Order Qty is Zero");

            // Random ID Generation
            const unsigned int _id = dist(gen);
    
            // Time
            std::time_t _time = time(0);

            // Ensure Ask price is greater than Best Bid
            if (_side == ASK && BidsBook.size() && (_price < BidsBook.peek())) 
                _price = BidsBook.peek();
            // Ensure Bid price is less than Best Ask
            if (_side == BID && AsksBook.size() && (_price > AsksBook.peek())) 
                _price = AsksBook.peek();
    
            // New Order
            std::shared_ptr<OrderInfo> new_order = std::make_shared<OrderInfo>(_side, _qty, _price, _id, _time);
    
            switch (_side)
            {
            case ASK:
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
            
            case BID:
                {
                    // Create new bid price level if no price level
                    BidLevels[_price].push_back(new_order);
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
                break;
            }
            notify_open(_id);
            recent_order_id = _id;
            book_updated = true;
            order_cv.notify_all(); // Wake Matching Engine
            order_cv.wait(lock, [this]{ 
                return !book_updated; 
            }); // Wait for matching engine
            return _id;
        }
        catch(std::exception &error)
        {
            std::cerr << "Order Error: " << error.what() << std::endl;
        }
        return 0;
    }

    // POST: Cancel Order
    void cancel_order(const unsigned int& _id)
    {
        // Mutex
        std::unique_lock<std::mutex> lock(order_lock);
        try
        {
            if (OrderTable.find(_id) == OrderTable.end()) 
                throw std::runtime_error("Order Does Not Exist");
            
            std::shared_ptr<OrderInfo> order = OrderTable[_id];
            OrderLevel &order_level = (order->side == BID) ?
            BidLevels[order->price] : AskLevels[order->price];

            // Iterate thruogh order level filtering out ORDER ID
            OrderLevel new_level;
            for (auto &cur_order : order_level)
            {
                if (cur_order->id != _id) new_level.push_back(cur_order);
            }
            order_level = std::move(new_level);

            // If Order Level is empty pop from Book and erase Order Level
            if (order_level.empty())
            {
                    if (order->side == BID)
                {
                    BidsBook.pop(BidsBook.find(order->price));
                    BidLevels.erase(order->price);
                }
                    else
                {
                    AsksBook.pop(AsksBook.find(order->price));
                    AskLevels.erase(order->price);
                }
            }

            notify_cancel(_id);
            OrderTable.erase(_id);
        }
        catch (std::exception &error)
        {
            std::cerr << "Order Error: " << error.what() << std::endl;
        }

        book_updated = true;
        order_cv.notify_one(); // Wake Engine
    }

    // PATCH: Edit Order
    unsigned int edit_order(const side_type& _side, const double& _qty, const double& _price, const unsigned int& _id)
    {
        cancel_order(_id);
        return place_order(_side, _qty, _price);
    }

    // GET: Get Order
    const std::shared_ptr<OrderInfo>* get_order(const unsigned int& _id) const
    { 
        try
        {
            const auto& order = OrderTable.find(_id);
            if (order == OrderTable.end())
                throw std::runtime_error("Order Does Not Exist");
            return &order->second; 
        }
        catch (const std::exception& error)
        {
            std::cout << error.what() << std::endl;
        }

        return NULL;
    }
    
    // GET: Average Price
    double get_price() const 
    {
        try
        {
            if (!(AsksBook.size() || BidsBook.size())) throw std::runtime_error("Asks or Bids Book is Empty");
            return (AsksBook.peek() + BidsBook.peek()) / 2; 
        }
        catch(const std::exception& e)
        {
            std::cerr << "Price Error: " << e.what() << '\n';
        }
        return -1;
    }

    // GET: Best Ask
    double get_best_ask() const 
    {
        try
        {
            if (!AsksBook.size()) throw std::runtime_error("Asks Book is Empty");
            return AsksBook.peek();
        }
        catch(const std::exception& e)
        {
            std::cerr << "Price Error: " << e.what() << '\n';
        }
        return -1;
    }

    // GET: Best Bid
    double get_best_bid() const 
    {
        try
        {
            if (!BidsBook.size()) throw std::runtime_error("Bids Book is Empty");
            return BidsBook.peek();
        }
        catch(const std::exception& e)
        {
            std::cerr << "Price Error: " << e.what() << '\n';
        }
        return -1;
    }

    // TODO: (GET: Maket Depth)
    std::vector<std::pair<double, std::size_t>> get_market_depth(side_type _side, std::size_t _depth = 10) const;
    
    // GET: Open Orders
    const OrderInfoList& get_open_orders() const { return OpenOrders; }
    
    // GET: Filled Orders
    const OrderInfoList& get_filled_orders() const { return FilledOrders; }
    
    // GET: Canceled Orders
    const OrderInfoList& get_canceled_orders() const { return CanceledOrders; }

private:
    // Order Book
    PriceHeap AsksBook; // Asks Order Book
    PriceHeap BidsBook; // Bids Order Book
    LevelMap AskLevels; // Asks Price Levels
    LevelMap BidLevels; // Bids Price Levels
    OrderMap OrderTable; // Map to all active orders
    unsigned int recent_order_id; // New Orders ID
    
    // Order Lists
    OrderInfoList OpenOrders; // Open Orders
    OrderInfoList FilledOrders; // Filled Orders
    OrderInfoList CanceledOrders; // Canceled Orders

    // Concurreny
    std::thread engine;
    std::mutex order_lock;
    std::condition_variable order_cv;
    std::atomic<bool> engine_running;
    bool book_updated;

    // Random
    std::mt19937_64 gen;
    std::uniform_int_distribution<unsigned int> dist;
    
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

            try
            {
                // If engine is off
                if (!engine_running) 
                {
                    book_updated = false;
                    order_cv.notify_one();
                }

                // If recent order is not Open
                if (OrderTable.find(recent_order_id) == OrderTable.end()) 
                    throw std::runtime_error("No Recent Order");

                // If Asks or Bids is Empty Continue
                if (!(AsksBook.size() && BidsBook.size())) 
                    throw std::runtime_error("Asks or Bids Book is Empty");
                
                // Get Recent Order
                std::shared_ptr<OrderInfo> recent_order = OrderTable[recent_order_id];
                while (recent_order && recent_order->qty)
                {
                    // Get best asks and bids
                    const double& best_asks_price = AsksBook.peek();
                    const double& best_bids_price = BidsBook.peek();
                    
                    // Get price level for best asks and bids
                    OrderLevel& best_level_asks = AskLevels[best_asks_price];
                    OrderLevel& best_level_bids = BidLevels[best_bids_price];
                    
                    // Break If you can't trade
                    const bool can_trade_asks = recent_order->side == ASK && !best_level_bids.empty() && 
                    best_level_bids.front()->price >= recent_order->price;
                    const bool can_trade_bids = recent_order->side == BID && !best_level_asks.empty() && 
                    best_level_asks.front()->price <= recent_order->price;
                    if (!(can_trade_asks || can_trade_bids)) break;
                    
                    // Get OrderInfo for best ask and bid
                    std::shared_ptr<OrderInfo>& best_ask = best_level_asks.front();
                    std::shared_ptr<OrderInfo>& best_bid = best_level_bids.front();

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
            } 
            catch (std::runtime_error &error)
            {
                std::cerr << "Matching Warning: " << error.what() << std::endl;
            }

            book_updated = false;
            order_cv.notify_one(); // Notify that the order has been processed
        }
    }

    // Match Orders
    void matching(std::shared_ptr<OrderInfo>& best_ask, std::shared_ptr<OrderInfo>& best_bid, OrderLevel& best_level_asks, OrderLevel& best_level_bids)
    {   
        // Get qty filled and apply difference
        const double qty_filled = std::min(best_ask->qty, best_bid->qty);
        best_ask->qty -= qty_filled;
        best_bid->qty -= qty_filled;
        
        // If ask qty is 0 then notify fill and erase
        if (!best_ask->qty)
        {
            notify_fill(best_ask->id, qty_filled);
            best_level_asks.pop_front();
            OrderTable.erase(best_ask->id);
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
            notify_fill(best_bid->id, qty_filled);
            best_level_bids.pop_front();
            OrderTable.erase(best_bid->id);
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
        try
        {
            if (OrderTable.find(_id) == OrderTable.end()) 
                throw std::runtime_error("Could Not Find Open Order");

            const std::shared_ptr<OrderInfo>& order = OrderTable[_id];
            const std::string& _side = order->side == BID ? "BUY" : "SELL";

            // Notification
            std::cout << "[OPEN] | " << "ID: " << order->id << " | SIDE: " << _side << " | QTY: " << order->qty << " | PRICE: "
            << order->price << " | TIME: "  << order->time << std::endl;

            OpenOrders.insert(
                std::tuple<std::time_t, unsigned int, std::string, double, double> 
                (order->time, order->id, _side, order->initial_qty, order->price)
            );
        }
        catch (std::exception &error)
        {
            std::cout << error.what() << std::endl;
        }
    }

    // Notify of what Orders were filled
    void notify_fill(const unsigned int& _id, const double& qty_filled)
    {
        try{
            if (OrderTable.find(_id) == OrderTable.end()) 
                throw std::runtime_error("Could Not Find Open Order");

            const std::shared_ptr<OrderInfo>& order = OrderTable[_id];
            const std::string& _side = order->side == BID ? "BUY" : "SELL";
            
            // Remove Order from OpenOrders Set
            auto open_order = OpenOrders.find(
                std::tuple<std::time_t, unsigned int, std::string, double, double>
                (order->time , order->id, _side, order->initial_qty, order->price)
            );

            if (open_order != OpenOrders.end()) OpenOrders.erase(open_order);
            else throw std::runtime_error("Could Not Find Open Order");
            
            // Time Order was Filled
            const std::time_t& _time = time(0);
            
            // Notification
            std::cout << "[FILLED] | " << "ID: " << order->id << " | SIDE: " << _side << " | QTY: " << qty_filled << " | PRICE: "
            << order->price << " | TIME: "  << _time << std::endl;
            
            FilledOrders.insert(
                std::tuple<std::time_t, unsigned int, std::string, double, double> 
                (_time, order->id, _side, qty_filled, order->price)
            );
        }
        catch (std::exception &error)
        {
            std::cout << "Notify Error: " << error.what() << std::endl;
        }
    }

    // Notify of what Orders were canceled
    void notify_cancel(const unsigned int& _id)
    {
        try
        {
            if (OrderTable.find(_id) == OrderTable.end()) 
                throw std::runtime_error("Could Not Find Open Order");

            const std::shared_ptr<OrderInfo>& order = OrderTable[_id];
            const std::string& _side = order->side == BID ? "BUY" : "SELL";
            
            // Remove Order from OpenOrders Set
            const auto& open_order = OpenOrders.find(
                std::tuple<std::time_t, unsigned int, std::string, double, double>
                (order->time ,order->id, _side, order->initial_qty, order->price)
            );

            if (open_order != OpenOrders.end()) OpenOrders.erase(open_order);
            else throw std::runtime_error("Could Not Find Open Order");
            
            // Time Order was Canceled
            const std::time_t& _time = time(0);
            
            // Notification
            std::cout << "[CANCELED] | " << "ID: " << order->id << " | SIDE: " << _side << " | QTY: " << order->qty << " | PRICE: "
            << order->price << " | TIME: "  << _time << std::endl;
            
            CanceledOrders.insert(
                std::tuple<std::time_t, unsigned int, std::string, double, double> 
                (_time, order->id, _side, order->initial_qty, order->price)
            );
        }
        catch (std::exception &error)
        {
            std::cout << error.what() << std::endl;
        }
    }
};


double truncateToHundredths(double value) {
    return ((int)(value * 100)) / 100.0;
}

void simulate_market_activity(OrderEngine& engine,
                              int num_orders = 10000,
                              double base_price = 100.0,
                              double volatility = 1.0,
                              double skew = -0.05)
{
    std::mt19937_64 rng(std::random_device{}());

    std::normal_distribution<double> skew_dist(skew, volatility);  // skew = mean, volatility = stddev
    std::uniform_real_distribution<double> qty_dist(1, 100);
    std::uniform_int_distribution<int> side_dist(0, 1);
    std::uniform_real_distribution<double> offset_dist(-5, 5);  // Local price jitter
    std::uniform_real_distribution<double> cancel_chance(0.0, 1.0); // 0 to 1
    double cancel_probability = 0.1; // Chance of Order Cancelling
    for (int i = 0; i < num_orders; ++i)
    { 
        base_price += skew_dist(rng);  // shift base price with skew + volatility

        side_type side = static_cast<side_type>(side_dist(rng));
        double qty = qty_dist(rng);
        double price = base_price + offset_dist(rng);  // jitter around base
        //price = truncateToHundredths(price);  // truncate to two decimal places
        unsigned int order_id = engine.place_order(side, qty, price);
        
        // Randomly cancel some orders
        if (cancel_chance(rng) < cancel_probability)
        {
            engine.cancel_order(order_id);
        }
    }

    double avg_price = engine.get_price();
    std::cout << "ENDING PRICE: " << avg_price << std::endl;
    std::cout << "OPEN ORDERS COUNT: " << engine.get_open_orders().size() << std::endl;
    std::cout << "FILLED ORDERS COUNT: " << engine.get_filled_orders().size() << std::endl;
    std::cout << "CANCELED ORDERS COUNT: " << engine.get_canceled_orders().size() << std::endl;
    std::cout << "BEST BID: " << engine.get_best_bid() << std::endl;
    std::cout << "BEST ASK: " << engine.get_best_ask() << std::endl;
}


int main()
{    
    OrderEngine OrderEngine;
    simulate_market_activity(OrderEngine, 10000, 100.0, 0.005, 0.05);
}