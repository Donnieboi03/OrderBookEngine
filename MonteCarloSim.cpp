#include "OrderEngine.cpp"

void monte_carlo_simulation(int num_orders = 10000, double base_price = 100.0, double volatility = 1.0,  double skew = -0.05)
{
    OrderEngine engine(base_price ,true); // Create Order Engine with verbose mode enabled
    std::mt19937_64 rng(std::random_device{}()); // Initialize random number generator

    // Price Distribution
    std::normal_distribution<double> skew_dist(skew, volatility);  // skew = mean, volatility = stddev
    std::uniform_real_distribution<double> qty_dist(1, 100); // Quantity Distribution
    std::uniform_real_distribution<double> offset_dist(-5, 5);  // Local price jitter
    
    // Order Side and Type Distributions
    std::uniform_int_distribution<int> side_dist(0, 1); // 0 for ASK, 1 for BID
    std::uniform_int_distribution<int> type_dist(0, 1); // 0 for LIMIT, 1 for MARKET
    
    // Cancel Order Probability Distribution
    std::uniform_real_distribution<double> cancel_chance(0.0, 1.0); // 0 to 1
    double cancel_probability = 0.05; // Chance of Order Cancelling

    for (int i = 0; i < num_orders; ++i)
    { 
        OrderSide side = static_cast<OrderSide>(side_dist(rng));
        OrderType type = static_cast<OrderType>(type_dist(rng));
        double qty = qty_dist(rng);

        double current_price = engine.get_price(); // Get current price from Order Engine
        double price = current_price + skew_dist(rng) + offset_dist(rng); // Get price with skew and offset
        price = std::max(price, 0.0); // Ensure price is non-negative
        
        const std::string order_type = type == OrderType::LIMIT ? "LIMIT" : "MARKET";
        std::cout << price << " " << order_type << std::endl;
        unsigned int order_id = engine.place_order(side, type, qty, price);
        // Randomly cancel some orders
        if (cancel_chance(rng) < cancel_probability)
           engine.cancel_order(order_id);
    }

    std::cout << "ENDING PRICE: " <<  engine.get_price() << std::endl;
    std::cout << "OPEN ORDERS COUNT: " << engine.get_orders_by_status(OrderStatus::OPEN).size() << std::endl;
    std::cout << "FILLED ORDERS COUNT: " << engine.get_orders_by_status(OrderStatus::FILLED).size() << std::endl;
    std::cout << "CANCELED ORDERS COUNT: " << engine.get_orders_by_status(OrderStatus::CANCELLED).size() << std::endl;
    std::cout << "BEST BID: " << engine.get_best_bid() << std::endl;
    std::cout << "BEST ASK: " << engine.get_best_ask() << std::endl;
}


int main()
{    
    monte_carlo_simulation(10000, 100.0, 0.0005, 0.0005);
}