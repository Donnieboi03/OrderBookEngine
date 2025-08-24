#include "Exchange.cpp"

// Print engine stats for a specific ticker
void print_stats(const std::string& ticker, const std::shared_ptr<OrderEngine>& engine) 
{
    std::cout << "=== STATS FOR " << ticker << " ===" << std::endl;
    std::cout << "CURRENT PRICE: " << engine->get_price() << std::endl;
    std::cout << "OPEN ORDERS COUNT: " << engine->get_orders_by_status(OrderStatus::OPEN).size() << std::endl;
    std::cout << "FILLED ORDERS COUNT: " << engine->get_orders_by_status(OrderStatus::FILLED).size() << std::endl;
    std::cout << "CANCELED ORDERS COUNT: " << engine->get_orders_by_status(OrderStatus::CANCELLED).size() << std::endl;
    std::cout << "REJECTED ORDERS COUNT: " << engine->get_orders_by_status(OrderStatus::REJECTED).size() << std::endl;
    std::cout << "BEST BID: " << engine->get_best_bid() << std::endl;
    std::cout << "BEST ASK: " << engine->get_best_ask() << std::endl;
    std::cout << "==============================" << std::endl;
}

// Monte Carlo Simulation for a single ticker
void monte_carlo_simulation(const std::shared_ptr<Exchange>& StockExchange, const std::string& ticker, int num_orders, 
double ipo_price, double ipo_qty, double volatility, double skew)
{
    StockExchange->initialize_stock(ticker, ipo_price, ipo_qty);
    std::mt19937_64 rng(std::random_device{}());

    std::normal_distribution<double> skew_dist(skew, volatility);
    std::uniform_real_distribution<double> qty_dist(1, 1000);
    std::uniform_real_distribution<double> offset_dist(-5, 5);
    std::uniform_int_distribution<int> side_dist(0, 1);
    std::uniform_int_distribution<int> type_dist(0, 1);
    std::uniform_real_distribution<double> cancel_chance(0.0, 1.0);

    double cancel_probability = 0.05;

    for (int i = 0; i < num_orders; ++i) {
        OrderSide side = static_cast<OrderSide>(side_dist(rng));
        OrderType type = static_cast<OrderType>(type_dist(rng));
        double qty = qty_dist(rng);

        double current_price = StockExchange->get_price(ticker);
        double price = current_price != -1 ? current_price + skew_dist(rng) + offset_dist(rng) : ipo_price;
        price = std::max(price, 0.0);
        unsigned int order_id;
        if (type == OrderType::MARKET)
            order_id = StockExchange->market_order(ticker, side, qty);
        else
            order_id = StockExchange->limit_order(ticker, side, price, qty);

        if (cancel_chance(rng) < cancel_probability)
            StockExchange->cancel_order(ticker, order_id);

        //std::this_thread::sleep_for(std::chrono::milliseconds(100)); // Sleep 100ms between orders
    }
}

// Main function to spawn threads for multiple tickers
int main() 
{
    std::shared_ptr<Exchange> DBSE = std::make_shared<Exchange>();
    std::vector<std::string> tickers = {"AAPL", "TSLA", "AMZN", "NVDA"};
    std::vector<std::thread> threads;

    for (const auto& ticker : tickers) {
        threads.emplace_back([=]() {
            monte_carlo_simulation(DBSE, ticker, 10000, 100.0, 10000, 0.5, 0.5);
        });
    }

    for (auto& t : threads) {
        t.join(); // Wait for all simulations to finish
    }

    // Print stats after all threads finish
    for (const auto& ticker : tickers) {
        print_stats(ticker, DBSE->get_engine(ticker)); // Assumes Exchange::get_engine(ticker) exists
    }
    return 0;
}
