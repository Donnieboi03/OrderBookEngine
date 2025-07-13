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

// Order Info
struct OrderInfo
{
    const side_type side;
    double qty;
    double price;
    const unsigned int id;
    const std::time_t time = std::time(0);
    

    OrderInfo(const side_type _side, double _qty, double _price, const unsigned int _id, const std::time_t _time) 
    : qty(_qty), price(_price), side(_side), id(_id), time(_time)
    {
    } 
};

// Order Level 
using OrderLevel = std::deque<std::shared_ptr<OrderInfo>>;

// Order Book
class PriceHeap
{
public:
    PriceHeap()
    : heap(0), min(true)
    {
    }

    PriceHeap(bool _min)
    : heap(0), min(_min)
    {
    }

    void push(const double data)
    {
        heap.push_back(data);
        heapify_up(heap.size() - 1);
    }

    double pop(const int index = 0)
    {
        try{
            if (!heap.size()) throw std::runtime_error("Empty Book");
            double popped_val = heap[index];
            std::swap(heap[index], heap[heap.size() - 1]);
            heap.pop_back();
            heapify_down(index);
            return popped_val;
        }
        catch(std::exception &error)
        {
            std::cerr << "Order Book Error: " << error.what() << std::endl;
        }
        return double();
    }

    const double peek() const 
    { 
        try{
            if (!heap.size()) throw std::runtime_error("Empty Book");
            return heap[0]; 
        }
        catch(std::exception &error)
        {
            std::cerr << "Order Book Error: " << error.what() << std::endl;
        }
        return double();
    }

    const int find(double data) const
    {
        try{
            if (!heap.size()) throw std::runtime_error("Empty Book");
            for (auto &x : heap)
            {
                if (x == data) return x;
            }
            throw std::runtime_error("Price Level Does Not Exist");
        }
        catch(std::exception &error)
        {
            std::cerr << "Order Book Error: " << error.what() << std::endl;
        }
        return double();

    }

    const int size() const { return heap.size(); }

private:
    std::vector<double> heap;
    const bool min;

    // For Pushing from the End
    void heapify_up(int index)
    {
        while (index > 0)
        {
            int parent = (index - 1) / 2;
            if (min * (heap[index] > heap[parent]) || !min * (heap[index] < heap[parent])) break;
            std::swap(heap[index], heap[parent]);
            index = parent;
        }
    }

    // For Popping from Front
    void heapify_down(int index)
    {
        while (index < heap.size())
        {
            int left_child = (index * 2) + 1 < heap.size() ? (index * 2) + 1 : index;
            int right_child = (index * 2) + 2 < heap.size() ? (index * 2) + 2 : index;

            int best_child = min * (heap[left_child] < heap[right_child]) || !min * (heap[left_child] > heap[right_child]) ?
            left_child : right_child;

            if (min * (heap[best_child] < heap[index]) || !min * (heap[best_child] > heap[index]))
            {
                std::swap(heap[best_child], heap[index]);
                index = best_child;
            }
            else{
                break;
            }
        }
    }
};

// Order API
class OrderEngine
{
public:
    OrderEngine() 
    : engine_running(true), book_updated(false), recent_order_id(0), gen(std::random_device{}()), dist(0, UINT32_MAX),
    AsksBook(true), BidsBook(false)
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

        // Time
        std::time_t _time = time(0);

        // New Order
        std::shared_ptr<OrderInfo> new_order = std::make_shared<OrderInfo>(_side, _qty, _price, _id, _time);

        switch (_side)
        {
        case ASK:
            {
                // Create price level if no price level
                if (AskLevels.find(_price) == AskLevels.end())
                {
                    OrderLevel new_level;
                    AsksBook.push(_price);
                    AskLevels[_price] = new_level;
                }
                AskLevels[_price].push_back(new_order);
                OrderTable[_id] = new_order;
                break;
            }
        
        case BID:
            {
                // Create price level if no price level
                if (BidLevels.find(_price) == BidLevels.end())
                {
                    OrderLevel new_level;
                    BidsBook.push(_price);
                    BidLevels[_price] = new_level;
                }
                BidLevels[_price].push_back(new_order);
                OrderTable[_id] = new_order;
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

    void cancel_order(const unsigned int _id)
    {
        // Mutex
        std::unique_lock<std::mutex> lock(order_lock);
        try
        {
            if (OrderTable.find(_id) == OrderTable.end()) throw("Order Does Not Exist");
            
            std::shared_ptr<OrderInfo> order = OrderTable[_id];
            
            // Get price level for best asks and bids
            OrderLevel *order_level = order->side == BID ?
            &BidLevels[order->price] : &AskLevels[order->price];

            // Itterate thruogh order level filtering out ORDER ID
            OrderLevel new_level;
            for (auto& cur_order : *order_level)
            {
                if (cur_order->id != _id) new_level.push_back(cur_order);
            }
            *order_level = std::move(new_level);

            // If Order Level is empty pop from Book and erase Order Level
            if (order_level->empty())
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

            // Erase Order
            OrderTable.erase(_id);
        }
        catch (std::exception &error)
        {
            std::cerr << error.what() << std::endl;
        }

        book_updated.store(true);
        order_processed.store(false); 
        order_cv.notify_one(); // Notify Engine
    }

    const unsigned int edit_order(const side_type _side, double _qty, double _price, const unsigned int _id)
    {
        cancel_order(_id);
        return place_order(_side, _qty, _price);
    }

private:
    // Order Book
    PriceHeap AsksBook; // Asks Order Book
    PriceHeap BidsBook; // Bids Order Book
    std::map<double, OrderLevel> AskLevels; // Asks Price Levels
    std::map<double, OrderLevel> BidLevels; // Bids Price Levels
    std::map<unsigned int, std::shared_ptr<OrderInfo>> OrderTable; // Map to all active orders
    std::vector<std::tuple<const unsigned int, const std::string, const double, const double, const std::time_t>> 
    FilledOrders; // Filled Orders
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
                OrderLevel &best_level_asks = AskLevels[best_asks_price];
                OrderLevel &best_level_bids = BidLevels[best_bids_price];
                
                // Get Recent Order
                std::shared_ptr<OrderInfo> recent_order = OrderTable[recent_order_id];
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
                std::cerr << "Matching Warning: " << error.what() << std::endl;
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
            AskLevels.erase(best_ask->price);
        }
        // If best bids level is empty then pop level and erase price map
        if (!best_level_bids.size())
        {
            BidsBook.pop();
            BidLevels.erase(best_bid->price);
        }
    }

    // Notify of what Orders were filled
    void notify(const unsigned int _id, const double qty_filled)
    {
        std::shared_ptr<OrderInfo> order = OrderTable[_id];
        std::string _side = order->side == BID ? "BUY" : "SELL";
        std::time_t _time = time(0);
        std::cout << "[FILLED] | " << "ID: " << order->id << " | SIDE: " << _side << " | QTY: " << qty_filled << " | PRICE: "
        << order->price << " | TIME: "  << _time << std::endl;
        std::tuple<const unsigned int, std::string, const double, const double, const std::time_t> 
        filled_order( order->id, _side, qty_filled, order->price, _time);
        FilledOrders.push_back(filled_order);
        OrderTable.erase(_id);
    }
};


int main()
{    
    OrderEngine OrderEngine;
    const unsigned int order_id = OrderEngine.place_order(BID, 10, 100);
    OrderEngine.edit_order(BID, 15, 100, order_id);
    OrderEngine.place_order(ASK, 5, 99);
    OrderEngine.place_order(ASK, 5, 100);
    OrderEngine.place_order(BID, 5, 101);    
}