import os
import sys
import pandas as pd
import matplotlib.pyplot as plt
import matplotlib.gridspec as gridspec

# ── Pre-flight check ──────────────────────────────────────────────────────────
# Check all required CSV files exist before doing anything.
# Without this, pandas throws a cryptic FileNotFoundError with no context.

REQUIRED = [
    "data/trade_log_ETH.csv",
    "data/trade_log_BTC.csv",
    "data/price_log_ETH.csv",
    "data/price_log_BTC.csv",
]

missing = [f for f in REQUIRED if not os.path.exists(f)]
if missing:
    print("\n  Error: the following data files are missing:")
    for f in missing:
        print(f"    {f}")
    print("\n  Run 'make run' first to generate them, then re-run this script.\n")
    sys.exit(1)

# ── Load data ─────────────────────────────────────────────────────────────────
eth_trades  = pd.read_csv("data/trade_log_ETH.csv")
btc_trades  = pd.read_csv("data/trade_log_BTC.csv")
eth_prices  = pd.read_csv("data/price_log_ETH.csv")
btc_prices  = pd.read_csv("data/price_log_BTC.csv")

# ── Derived metrics ───────────────────────────────────────────────────────────
def bot_pnl(trades, prices):
    """Compute rough cumulative P&L proxy per bot from trade log."""
    last_price = prices["price"].iloc[-1]
    pnl = {}
    for bot in trades["buyer"].unique():
        bought = trades[trades["buyer"] == bot]["quantity"].sum()
        sold   = trades[trades["seller"] == bot]["quantity"].sum()
        net    = bought - sold
        pnl[bot] = net * last_price * 0.001  # rough proxy
    return pnl

def trade_count_per_bot(trades):
    buyers  = trades["buyer"].value_counts()
    sellers = trades["seller"].value_counts()
    return (buyers.add(sellers, fill_value=0)).sort_values(ascending=False)

# ── Plot ──────────────────────────────────────────────────────────────────────
fig = plt.figure(figsize=(18, 14))
fig.patch.set_facecolor("#0d1117")
gs  = gridspec.GridSpec(3, 2, figure=fig, hspace=0.45, wspace=0.35)

COLORS = ["#58a6ff", "#3fb950", "#f85149", "#d29922", "#a371f7", "#79c0ff"]

def style_ax(ax, title):
    ax.set_facecolor("#161b22")
    ax.set_title(title, color="#c9d1d9", fontsize=11, fontweight="bold", pad=10)
    ax.tick_params(colors="#8b949e")
    ax.xaxis.label.set_color("#8b949e")
    ax.yaxis.label.set_color("#8b949e")
    for spine in ax.spines.values():
        spine.set_edgecolor("#30363d")

# ── 1. ETH price over time ────────────────────────────────────────────────────
ax1 = fig.add_subplot(gs[0, 0])
ax1.plot(eth_prices["timestep"], eth_prices["price"],
         color="#58a6ff", linewidth=1.2, label="ETH/USDT")
ax1.fill_between(eth_prices["timestep"], eth_prices["price"],
                 alpha=0.15, color="#58a6ff")
ax1.set_xlabel("Timestep")
ax1.set_ylabel("Price ($)")
style_ax(ax1, "ETH/USDT Price")
ax1.legend(facecolor="#161b22", edgecolor="#30363d", labelcolor="#c9d1d9")

# ── 2. BTC price over time ────────────────────────────────────────────────────
ax2 = fig.add_subplot(gs[0, 1])
ax2.plot(btc_prices["timestep"], btc_prices["price"],
         color="#f85149", linewidth=1.2, label="BTC/USDT")
ax2.fill_between(btc_prices["timestep"], btc_prices["price"],
                 alpha=0.15, color="#f85149")
ax2.set_xlabel("Timestep")
ax2.set_ylabel("Price ($)")
style_ax(ax2, "BTC/USDT Price")
ax2.legend(facecolor="#161b22", edgecolor="#30363d", labelcolor="#c9d1d9")

# ── 3. ETH trade volume per bot ───────────────────────────────────────────────
ax3 = fig.add_subplot(gs[1, 0])
eth_counts = trade_count_per_bot(eth_trades)
bars = ax3.bar(eth_counts.index, eth_counts.values,
               color=COLORS[:len(eth_counts)], edgecolor="#30363d", linewidth=0.5)
ax3.set_xlabel("Bot")
ax3.set_ylabel("Trade Count")
style_ax(ax3, "ETH — Trades per Bot")
ax3.tick_params(axis="x", rotation=30)
for bar, val in zip(bars, eth_counts.values):
    ax3.text(bar.get_x() + bar.get_width()/2, bar.get_height() + 0.3,
             str(int(val)), ha="center", va="bottom",
             color="#c9d1d9", fontsize=8)

# ── 4. BTC trade volume per bot ───────────────────────────────────────────────
ax4 = fig.add_subplot(gs[1, 1])
btc_counts = trade_count_per_bot(btc_trades)
bars = ax4.bar(btc_counts.index, btc_counts.values,
               color=COLORS[:len(btc_counts)], edgecolor="#30363d", linewidth=0.5)
ax4.set_xlabel("Bot")
ax4.set_ylabel("Trade Count")
style_ax(ax4, "BTC — Trades per Bot")
ax4.tick_params(axis="x", rotation=30)
for bar, val in zip(bars, btc_counts.values):
    ax4.text(bar.get_x() + bar.get_width()/2, bar.get_height() + 0.3,
             str(int(val)), ha="center", va="bottom",
             color="#c9d1d9", fontsize=8)

# ── 5. ETH trade price distribution ──────────────────────────────────────────
ax5 = fig.add_subplot(gs[2, 0])
ax5.hist(eth_trades["price"], bins=40, color="#58a6ff",
         edgecolor="#0d1117", linewidth=0.3, alpha=0.85)
ax5.axvline(eth_trades["price"].mean(), color="#d29922",
            linestyle="--", linewidth=1.2, label=f"Mean ${eth_trades['price'].mean():.2f}")
ax5.set_xlabel("Fill Price ($)")
ax5.set_ylabel("Frequency")
style_ax(ax5, "ETH — Fill Price Distribution")
ax5.legend(facecolor="#161b22", edgecolor="#30363d", labelcolor="#c9d1d9")

# ── 6. BTC trade price distribution ──────────────────────────────────────────
ax6 = fig.add_subplot(gs[2, 1])
ax6.hist(btc_trades["price"], bins=40, color="#f85149",
         edgecolor="#0d1117", linewidth=0.3, alpha=0.85)
ax6.axvline(btc_trades["price"].mean(), color="#d29922",
            linestyle="--", linewidth=1.2, label=f"Mean ${btc_trades['price'].mean():.2f}")
ax6.set_xlabel("Fill Price ($)")
ax6.set_ylabel("Frequency")
style_ax(ax6, "BTC — Fill Price Distribution")
ax6.legend(facecolor="#161b22", edgecolor="#30363d", labelcolor="#c9d1d9")

# ── Title ─────────────────────────────────────────────────────────────────────
fig.suptitle("C++ Multi-Agent Trading Simulator — Performance Dashboard",
             color="#c9d1d9", fontsize=14, fontweight="bold", y=0.98)

# ── Summary stats to terminal ─────────────────────────────────────────────────
total_eth = len(eth_trades)
total_btc = len(btc_trades)
print(f"\n  ETH: {total_eth} trades across {eth_trades['buyer'].nunique()} bots")
print(f"  BTC: {total_btc} trades across {btc_trades['buyer'].nunique()} bots")
print(f"  ETH avg fill: ${eth_trades['price'].mean():.2f}  "
      f"min: ${eth_trades['price'].min():.2f}  "
      f"max: ${eth_trades['price'].max():.2f}")
print(f"  BTC avg fill: ${btc_trades['price'].mean():.2f}  "
      f"min: ${btc_trades['price'].min():.2f}  "
      f"max: ${btc_trades['price'].max():.2f}")

plt.savefig("data/dashboard.png", dpi=150, bbox_inches="tight",
            facecolor=fig.get_facecolor())
print("\n  Saved: data/dashboard.png\n")
plt.show()