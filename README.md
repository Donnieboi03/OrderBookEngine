# ⚙️ Jarvis Matching Engine – High-Performance C++ Order Book

A **multithreaded**, **low-latency** matching engine built from scratch in modern C++. Jarvis simulates a financial exchange order book using custom heap-based price levels, real-time trade matching, and concurrency-safe order handling. Designed for simulation, backtesting, and educational purposes in algorithmic or high-frequency trading environments.

---

## 🚀 Key Features

### 💡 Core Functionality
- **Custom Heap-based Price Management**  
  Dual min/max heaps for ask/bid order books with dynamic `heapify_up`/`heapify_down` logic.
  
- **Limit Order Matching Engine**  
  Full support for limit orders (BID/ASK) with quantity-based matching.

- **Thread-Safe Execution**  
  Uses `std::thread`, `std::mutex`, and `std::condition_variable` to ensure safe concurrent access.

- **Order Lifecycle Operations**  
  - `place_order()`: Submit a new buy/sell order  
  - `cancel_order()`: Cancel open orders by ID  
  - `edit_order()`: Modify price or quantity of existing orders

### 📡 Real-Time Monitoring
- **Live Notifications**  
  Console outputs for all order events (`[OPEN]`, `[FILLED]`, `[CANCELED]`).

- **Order History Tracking**  
  Maintains sets for open, filled, and canceled orders.

- **Recent Price Metrics**  
  Functions like `get_price()`, `get_best_bid()`, and `get_best_ask()` return accurate current levels.

### 🧪 Simulation Tools
- **Market Simulation Module**  
  `simulate_market_activity()` generates thousands of randomized BID/ASK orders for testing.

---

## 🛠 Tech Stack

| Library | Use |
|--------|-----|
| `<thread>` / `<mutex>` | Safe multithreading |
| `<vector>`, `<deque>` | Price and order levels |
| `<map>`, `<unordered_map>` | Efficient data indexing |
| `<random>` | Randomized simulation |
| `<memory>` | Smart pointers for order tracking |

---

## 📈 Example Output

