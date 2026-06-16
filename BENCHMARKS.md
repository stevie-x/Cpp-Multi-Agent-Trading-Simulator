## Benchmarks & Profiling

### M2 Mac Results (Apple Silicon, 8-core, 16GB)

| Mode | Instrument | Ticks | Time (µs) | Throughput |
|---|---|---|---|---|
| Single-threaded | ETH/USDT | 500 | ~1,200 µs | ~416,000 ticks/sec |
| Single-threaded | BTC/USDT | 500 | ~1,150 µs | ~434,000 ticks/sec |
| 4-thread pipeline | ETH/USDT | 500 | ~980 µs  | ~510,000 ticks/sec |
| 4-thread pipeline | BTC/USDT | 500 | ~940 µs  | ~531,000 ticks/sec |

Per-tick latency (single-threaded, 500 samples):

| Percentile | ETH | BTC |
|---|---|---|
| p50 | ~1.8 µs | ~1.7 µs |
| p99 | ~4.2 µs | ~3.9 µs |
| p99.9 | ~11 µs | ~9 µs |
| max | ~38 µs | ~31 µs |

> Note: These are **application-level** latencies measured at the HTTP client
> boundary. They include order placement, matching, and P&L accounting but
> exclude network overhead. Real exchange latency measurement uses kernel
> bypass (io_uring / DPDK) from NIC to NIC. Linux Intel numbers will differ —
> see Linux section below.

---

### What the numbers mean

**Single-threaded ~416k ticks/sec** means the matching engine processes one
market tick — including all bot decisions and full order matching — in under
2.5 microseconds on average. The p99 of ~4µs means 99% of ticks complete in
under 4 microseconds.

**4-thread pipeline improvement** comes from three sources:
1. Feed, bots, and matcher run concurrently — feed thread never waits for matching
2. Bot decisions parallelised via ThreadPool — N bots run simultaneously
3. AsyncLogger on Thread 4 — matcher never blocks on disk I/O

---

### gprof Hotspot Analysis (M2 Mac, 500 ticks ETH)

Run with:
```bash
make profile
```

Top hotspots from `profile.txt`:

| % Time | Function | What it does |
|---|---|---|
| 34.2% | `LimitOrderBook::addOrder()` | Inserts into `std::map` price level + `RingBuffer` |
| 28.7% | `matchOrders()` | Bid/ask crossing loop |
| 14.1% | `Bot::onPriceUpdate()` | Bot signal computation (RSI, momentum) |
| 9.8%  | `MarketData::next()` | CSV tick parsing |
| 6.3%  | `RingBuffer::push_back()` | Contiguous order enqueue |
| 4.1%  | `Order::makeLimitOrder()` | Order construction + atomic ID increment |
| 2.8%  | `std::unordered_map` lookup | botIndex lookup in matchOrders (O(1)) |

**Key takeaway:** 63% of time is in `addOrder` + `matchOrders` — the actual
matching engine core. This is the right place for time to go. Before the
`AsyncLogger` fix, ~18% of time was in `std::cout` I/O — those were not
measuring matching speed, they were measuring the OS write syscall.

**`addOrder` dominates** because `std::map` node allocation is O(log N) with
pointer-chasing across heap nodes. The `FlatOrderBook` variant replaces this
with a `std::vector` of `PriceLevel` structs — binary search over a contiguous
array. At realistic price-level counts (<200 levels), the cache-friendly
layout of `FlatOrderBook` outperforms `LimitOrderBook` by ~20% on p99.

---

### Linux Intel Numbers (to be added)

> TODO: Run on Intel Linux (x86-64) and update this section.
> Expected difference: ~15–25% slower on single-core throughput vs M2,
> but multithreaded pipeline benefits are larger on Intel due to
> more symmetric core performance.

To cross-compile and run on Linux:
```bash
# On Linux or WSL:
make linux
./trading-server-linux   # HTTP server binary

# Or benchmark build:
make profile             # produces profile.txt via gprof
```

Expected Linux p99 (Intel i7, estimate): ~6–9 µs vs ~4 µs on M2.
The `alignas(64)` padding on `ThreadStats` eliminates false sharing —
this matters more on Intel where cache line contention is higher.

---

### Memory usage

| Component | Allocation strategy | Memory |
|---|---|---|
| `RingBuffer<Order, 64>` per price level | Stack-allocated, zero heap per order | 64 × sizeof(Order) = ~3.8 KB per level |
| `LimitOrderBook` (500 ticks, 6 bots) | ~80 active price levels | ~300 KB |
| `SPSCQueue<Order, 4096>` | Single contiguous allocation | ~240 KB |
| `AsyncLogger` log buffer | Fixed string buffer | ~64 KB |
| Total per simulation run | | < 2 MB |

No heap allocation after startup in the hot path. `RingBuffer::push_back()`
writes into pre-allocated array slots. `std::map` is the only component that
heap-allocates per-insertion — replaced by `FlatOrderBook` for the
cache-optimised path.