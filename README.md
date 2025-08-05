# ⚙️ High-Performance C++ Order Book Matching Engine

A **multithreaded**, **low-latency** order book engine built from scratch in modern C++ for **market microstructure simulation**.  
This engine powers a simulated financial exchange capable of handling **concurrent order flows, real-time trade matching**, and **Monte Carlo-driven market activity**.

---

## 🚀 Key Features
### 🧪 Simulation Tools
- **Monte Carlo Simulation Module**  
  `monte_carlo_simulation()` generates thousands of randomized BID/ASK orders for generating orderbook microstructure.
  
### 💡 Core Functionality
- **Thread-Safe Execution**  
  Uses `std::thread`, `std::mutex`, and `std::condition_variable` to ensure safe concurrent access.

- **Order Lifecycle Operations**  
  - `place_order()`: Submit a new buy/sell order  
  - `cancel_order()`: Cancel open orders by ID  
  - `edit_order()`: Modify price or quantity of existing orders
    
- **Limit/Market Order Matching Engine**  
  Full support for **limit** and **market** orders (BID/ASK) with quantity-based matching.

### 📡 Real-Time Monitoring
- **Live Notifications**  
  Console outputs for all order events (`[OPEN]`, `[FILLED]`, `[PARTIALY FILLED]`,`[CANCELED]`).

- **Custom Heap-based Price Management**  
  Dual min/max heaps for ask/bid order books with dynamic `heapify_up`/`heapify_down` logic.

- **Recent Price Metrics**  
  Functions like `get_price()`, `get_best_bid()`, and `get_best_ask()` return accurate current levels.

---

## 🛠 Tech Stack

| Library | Use |
|--------|-----|
| `<thread>` / `<mutex>` / `<atomic>`| Safe multithreading |
| `<vector>`, `<deque>` | Price and order levels |
| `<map>`, `<set>` | Efficient data indexing |
| `<random>` | Randomized simulation |
| `<memory>` | Smart pointers for order tracking |

---
