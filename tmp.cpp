#include <iostream>
#include <set>
#include <unordered_map>
#include <deque>
#include <thread>
#include <mutex>
#include <atomic>
#include <memory>
#include <random>
#include <ctime>


enum class OrderType
{
    LIMIT,
    MARKET
};

enum class OrderStatus
{
    PENDING,
    EXECUTED,
    CANCELLED
};

enum class OrderSide
{
    BUY,
    SELL
};

struct Order
{
    u_int32_t id;
    OrderType type;
    OrderStatus status;
    OrderSide side;
    double_t price;
    double_t quantity;
    std::time_t timestamp;
    
    Order(u_int32_t _id, OrderType _type, OrderStatus _status, OrderSide _side, double_t _price, double_t _quantity)
        : id(_id), type(_type), status(_status), side(_side), price(_price), quantity(_quantity), timestamp(std::time(nullptr)) 
        {}
};

using Order_ptr_t = std::shared_ptr<Order>;
using OrderTable = std::unordered_map<u_int32_t, Order_ptr_t>;
using OrderLevel = std::deque<Order_ptr_t>;
using LevelMap = std::unordered_map<double, OrderLevel>;







int main()
{

}