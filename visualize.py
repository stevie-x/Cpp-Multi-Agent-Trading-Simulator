import pandas as pd
import numpy as np
import matplotlib.pyplot as plt
import matplotlib.gridspec as gridspec
import matplotlib.patches as mpatches
from matplotlib.colors import to_rgba
import os
import sys

# ── Config ─────────────────────────────────────────────────────────────────────
TRAIN_PCT   = 0.70   # first 70% of trades = train, last 30% = test
COLORS      = {
    "Random_A" : "#2196F3",
    "Random_B" : "#03A9F4",
    "Momentum" : "#FF5722",
    "RSI"      : "#4CAF50",
    "Bollinger": "#9C27B0",
    "Imbalance": "#FF9800",
}
DEFAULT_COLOR = "#607D8B"

def bot_color(name):
    for key, col in COLORS.items():
        if key in name:
            return col
    return DEFAULT_COLOR

# ── Load data ──────────────────────────────────────────────────────────────────
trade_file = "data/trade_log_ETH.csv"
price_file = "data/price_log_ETH.csv"

if not os.path.exists(trade_file):
    print(f"ERROR: {trade_file} not found. Run ./trading_sim first.")
    sys.exit(1)

trades = pd.read_csv(trade_file)
prices = pd.read_csv(price_file) if os.path.exists(price_file) else None

print(f"Loaded {len(trades)} trades, {len(prices) if prices is not None else 0} price ticks")

bots = sorted(set(trades["buyer"].tolist() + trades["seller"].tolist()))
print(f"Bots: {bots}")

# ── Walk-forward split ─────────────────────────────────────────────────────────
# Split trades by timestep, not by row count.
# Train = first 70% of timesteps. Test = last 30%.
# This is the correct way — splitting by row would leak future data.
#
# For each segment we compute PnL, Sharpe, MaxDrawdown, WinRate independently.
# If a strategy's Sharpe collapses from train to test → curve-fitted.
# If it holds or improves → genuine edge (or lucky, but more credible).

max_ts    = trades["timestep"].max()
split_ts  = int(max_ts * TRAIN_PCT)

train_trades = trades[trades["timestep"] <= split_ts].copy()
test_trades  = trades[trades["timestep"] >  split_ts].copy()

print(f"\nWalk-Forward Split:")
print(f"  Train: timesteps 1–{split_ts}   ({len(train_trades)} trades)")
print(f"  Test:  timesteps {split_ts+1}–{max_ts} ({len(test_trades)} trades)")

# ── Metric helpers ─────────────────────────────────────────────────────────────
def compute_pnl_series(bot, df):
    """Cumulative PnL series for a bot over a trade dataframe."""
    pnl = 0.0
    series = []
    for _, row in df.iterrows():
        if row["buyer"] == bot:
            pnl -= row["price"] * row["quantity"]
        elif row["seller"] == bot:
            pnl += row["price"] * row["quantity"]
        series.append(pnl)
    return series

def sharpe(series):
    s = pd.Series(series).diff().dropna()
    return (s.mean() / s.std() * np.sqrt(252)) if s.std() > 0 else 0.0

def max_drawdown(series):
    s = pd.Series(series)
    return float((s - s.cummax()).min())

def win_rate(bot, df):
    sells = len(df[df["seller"] == bot])
    total = len(df[(df["buyer"] == bot) | (df["seller"] == bot)])
    return (sells / total * 100) if total > 0 else 0.0

def final_pnl(series):
    return series[-1] if series else 0.0

# ── Compute metrics for both segments ─────────────────────────────────────────
def segment_metrics(df, label):
    rows = []
    for bot in bots:
        series = compute_pnl_series(bot, df)
        if not series:
            series = [0.0]
        rows.append({
            "Bot"        : bot,
            "Segment"    : label,
            "Final PnL"  : round(final_pnl(series), 2),
            "Sharpe"     : round(sharpe(series), 3),
            "MaxDrawdown": round(max_drawdown(series), 2),
            "WinRate%"   : round(win_rate(bot, df), 1),
            "Trades"     : len(df[(df["buyer"] == bot) | (df["seller"] == bot)]),
            "_series"    : series,
        })
    return pd.DataFrame(rows)

train_metrics = segment_metrics(train_trades, "Train")
test_metrics  = segment_metrics(test_trades,  "Test")

# ── Print comparison table ─────────────────────────────────────────────────────
print("\n" + "="*80)
print("  WALK-FORWARD VALIDATION — STRATEGY COMPARISON")
print("="*80)
print(f"  {'Bot':<14} {'Train PnL':>10} {'Test PnL':>10} {'Train Sharpe':>13} "
      f"{'Test Sharpe':>12} {'Train WR%':>10} {'Test WR%':>9} {'Verdict':>10}")
print("-"*80)

for bot in bots:
    tr = train_metrics[train_metrics["Bot"] == bot].iloc[0]
    te = test_metrics [test_metrics ["Bot"] == bot].iloc[0]

    # Verdict: did the strategy hold up out-of-sample?
    sharpe_ratio = te["Sharpe"] / tr["Sharpe"] if tr["Sharpe"] != 0 else 0
    if   sharpe_ratio > 0.7 : verdict = "✅ HOLDS"
    elif sharpe_ratio > 0.3 : verdict = "⚠️  DEGRADES"
    else                    : verdict = "❌ OVERFITS"

    print(f"  {bot:<14} {tr['Final PnL']:>10.2f} {te['Final PnL']:>10.2f} "
          f"{tr['Sharpe']:>13.3f} {te['Sharpe']:>12.3f} "
          f"{tr['WinRate%']:>10.1f} {te['WinRate%']:>9.1f} {verdict:>10}")

print("="*80)
print("  Verdict: Sharpe(test)/Sharpe(train) > 0.7 = holds, 0.3-0.7 = degrades, <0.3 = overfit")

# ── Full PnL series for all trades (for the curve chart) ──────────────────────
all_series = {}
for bot in bots:
    all_series[bot] = compute_pnl_series(bot, trades)

# ── Dashboard layout ───────────────────────────────────────────────────────────
fig = plt.figure(figsize=(20, 14))
fig.suptitle("Multi-Agent Trading Simulator — Walk-Forward Dashboard",
             fontsize=15, fontweight="bold", y=0.98)

gs = gridspec.GridSpec(4, 3, figure=fig, hspace=0.55, wspace=0.35)

# ── 1. ETH Price (full width, top) ────────────────────────────────────────────
ax_price = fig.add_subplot(gs[0, :])
if prices is not None:
    ax_price.plot(prices["timestep"], prices["price"],
                  color="#FF9800", linewidth=1.5, label="ETH Price")
    ax_price.axvline(split_ts, color="red", linestyle="--",
                     linewidth=1.2, label=f"Train/Test split (t={split_ts})")
    ax_price.legend(fontsize=9)
ax_price.set_title("ETH/USDT Market Price")
ax_price.set_xlabel("Timestep")
ax_price.set_ylabel("Price (USDT)")
ax_price.grid(True, alpha=0.3)

# ── 2. Cumulative PnL curves ──────────────────────────────────────────────────
ax_pnl = fig.add_subplot(gs[1, :2])
for bot in bots:
    series = all_series[bot]
    if not series: continue
    ax_pnl.plot(series, label=bot, color=bot_color(bot), linewidth=1.8)
ax_pnl.axvline(int(len(trades) * TRAIN_PCT), color="red",
               linestyle="--", linewidth=1, alpha=0.7, label="Train/Test split")
ax_pnl.axhline(0, color="gray", linestyle="--", linewidth=0.8)
ax_pnl.set_title("Cumulative PnL — All Trades")
ax_pnl.set_xlabel("Trade #")
ax_pnl.set_ylabel("PnL (USDT)")
ax_pnl.legend(fontsize=8)
ax_pnl.grid(True, alpha=0.3)

# ── 3. Train vs Test PnL bar chart ────────────────────────────────────────────
ax_compare = fig.add_subplot(gs[1, 2])
x      = np.arange(len(bots))
width  = 0.35
tr_pnl = [train_metrics[train_metrics["Bot"]==b]["Final PnL"].values[0] for b in bots]
te_pnl = [test_metrics [test_metrics ["Bot"]==b]["Final PnL"].values[0] for b in bots]

ax_compare.bar(x - width/2, tr_pnl, width, label="Train",
               color=[bot_color(b) for b in bots], alpha=0.85)
ax_compare.bar(x + width/2, te_pnl, width, label="Test",
               color=[bot_color(b) for b in bots], alpha=0.45)
ax_compare.axhline(0, color="gray", linestyle="--", linewidth=0.8)
ax_compare.set_title("Train vs Test PnL")
ax_compare.set_xticks(x)
ax_compare.set_xticklabels([b.replace("_", "\n") for b in bots], fontsize=7)
ax_compare.legend(fontsize=8)
ax_compare.grid(True, alpha=0.3, axis="y")

# ── 4. Sharpe: Train vs Test ──────────────────────────────────────────────────
ax_sharpe = fig.add_subplot(gs[2, 0])
tr_sh = [train_metrics[train_metrics["Bot"]==b]["Sharpe"].values[0] for b in bots]
te_sh = [test_metrics [test_metrics ["Bot"]==b]["Sharpe"].values[0] for b in bots]
ax_sharpe.bar(x - width/2, tr_sh, width, label="Train",
              color=[bot_color(b) for b in bots], alpha=0.85)
ax_sharpe.bar(x + width/2, te_sh, width, label="Test",
              color=[bot_color(b) for b in bots], alpha=0.45)
ax_sharpe.axhline(0, color="gray", linestyle="--", linewidth=0.8)
ax_sharpe.set_title("Sharpe Ratio: Train vs Test")
ax_sharpe.set_xticks(x)
ax_sharpe.set_xticklabels([b.replace("_", "\n") for b in bots], fontsize=7)
ax_sharpe.legend(fontsize=8)
ax_sharpe.grid(True, alpha=0.3, axis="y")

# ── 5. Win Rate: Train vs Test ────────────────────────────────────────────────
ax_wr = fig.add_subplot(gs[2, 1])
tr_wr = [train_metrics[train_metrics["Bot"]==b]["WinRate%"].values[0] for b in bots]
te_wr = [test_metrics [test_metrics ["Bot"]==b]["WinRate%"].values[0] for b in bots]
ax_wr.bar(x - width/2, tr_wr, width, label="Train",
          color=[bot_color(b) for b in bots], alpha=0.85)
ax_wr.bar(x + width/2, te_wr, width, label="Test",
          color=[bot_color(b) for b in bots], alpha=0.45)
ax_wr.set_title("Win Rate %: Train vs Test")
ax_wr.set_ylim(0, 110)
ax_wr.set_xticks(x)
ax_wr.set_xticklabels([b.replace("_", "\n") for b in bots], fontsize=7)
ax_wr.legend(fontsize=8)
ax_wr.grid(True, alpha=0.3, axis="y")

# ── 6. Max Drawdown: Train vs Test ────────────────────────────────────────────
ax_dd = fig.add_subplot(gs[2, 2])
tr_dd = [train_metrics[train_metrics["Bot"]==b]["MaxDrawdown"].values[0] for b in bots]
te_dd = [test_metrics [test_metrics ["Bot"]==b]["MaxDrawdown"].values[0] for b in bots]
ax_dd.bar(x - width/2, tr_dd, width, label="Train",
          color=[bot_color(b) for b in bots], alpha=0.85)
ax_dd.bar(x + width/2, te_dd, width, label="Test",
          color=[bot_color(b) for b in bots], alpha=0.45)
ax_dd.set_title("Max Drawdown: Train vs Test")
ax_dd.set_xticks(x)
ax_dd.set_xticklabels([b.replace("_", "\n") for b in bots], fontsize=7)
ax_dd.legend(fontsize=8)
ax_dd.grid(True, alpha=0.3, axis="y")

# ── 7. Walk-forward verdict table ─────────────────────────────────────────────
ax_table = fig.add_subplot(gs[3, :])
ax_table.axis("off")

table_data = []
col_labels = ["Bot", "Train PnL", "Test PnL", "Train Sharpe",
              "Test Sharpe", "Train WR%", "Test WR%", "Verdict"]

for bot in bots:
    tr = train_metrics[train_metrics["Bot"] == bot].iloc[0]
    te = test_metrics [test_metrics ["Bot"] == bot].iloc[0]
    sharpe_ratio = te["Sharpe"] / tr["Sharpe"] if tr["Sharpe"] != 0 else 0
    if   sharpe_ratio > 0.7 : verdict = "✅ HOLDS"
    elif sharpe_ratio > 0.3 : verdict = "⚠ DEGRADES"
    else                    : verdict = "❌ OVERFITS"
    table_data.append([
        bot,
        f"${tr['Final PnL']:,.0f}",
        f"${te['Final PnL']:,.0f}",
        f"{tr['Sharpe']:.2f}",
        f"{te['Sharpe']:.2f}",
        f"{tr['WinRate%']:.0f}%",
        f"{te['WinRate%']:.0f}%",
        verdict,
    ])

tbl = ax_table.table(cellText=table_data, colLabels=col_labels,
                     loc="center", cellLoc="center")
tbl.auto_set_font_size(False)
tbl.set_fontsize(9)
tbl.scale(1, 1.6)

# Color header
for j in range(len(col_labels)):
    tbl[(0, j)].set_facecolor("#263238")
    tbl[(0, j)].set_text_props(color="white", fontweight="bold")

# Color verdict cells
for i, bot in enumerate(bots):
    verdict_cell = tbl[(i+1, 7)]
    text = verdict_cell.get_text().get_text()
    if "HOLDS"   in text: verdict_cell.set_facecolor("#C8E6C9")
    elif "DEGRA" in text: verdict_cell.set_facecolor("#FFF9C4")
    else                : verdict_cell.set_facecolor("#FFCDD2")

ax_table.set_title("Walk-Forward Validation Summary", fontsize=11,
                   fontweight="bold", pad=12)

# ── Save ───────────────────────────────────────────────────────────────────────
out = "data/performance_dashboard.png"
plt.savefig(out, dpi=150, bbox_inches="tight")
plt.show()
print(f"\nDashboard saved to {out}")