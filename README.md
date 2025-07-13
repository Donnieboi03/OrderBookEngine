#⚙️ High-Performance C++ Matching Engine

A multithreaded, low-latency matching engine for simulating a financial order book. Built entirely in modern C++ with custom heap-based order books for managing price levels and thread-safe execution.

🧠 Features:

- 🏛 *Custom-built OrderBook with min/max heap logic*

- 🧵 *Thread-safe matching engine using std::thread, mutex, and condition_variable*

- 💹 *Support for limit orders (BID/ASK) with real-time matching*

- 🗑 *Order placement, editing, and cancellation*

- 📊 *Efficient price level management with std::map<double, OrderLevel>*

- 🛠 *Simulated exchange behavior suitable for HFT backtesting, learning, or integration into trading bots*
