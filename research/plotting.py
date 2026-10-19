"""Static charts for reports and notebooks (matplotlib).

Visual rules: one y-axis per panel (stack panels instead of dual axes),
thin 2px lines, recessive grid, colour follows the strategy everywhere
(market maker = blue, momentum = orange), diverging scales centred on zero
with a neutral grey midpoint, and a legend whenever there is >1 series.
"""

from __future__ import annotations

import sys
from typing import Mapping

import matplotlib

if "ipykernel" not in sys.modules:  # headless scripts/CI; notebooks keep the inline backend
    matplotlib.use("Agg")

import matplotlib.pyplot as plt  # noqa: E402
import numpy as np  # noqa: E402
import pandas as pd  # noqa: E402
from matplotlib.colors import LinearSegmentedColormap, TwoSlopeNorm  # noqa: E402
from matplotlib.ticker import FuncFormatter  # noqa: E402

# Validated categorical palette (light surface) + chart chrome.
SURFACE = "#fcfcfb"
INK = "#0b0b0b"
INK_2 = "#52514e"
MUTED = "#898781"
GRID = "#e1e0d9"
AXIS = "#c3c2b7"
BLUE, ORANGE, AQUA, VIOLET = "#2a78d6", "#eb6834", "#1baf7a", "#6250d6"

STRATEGY_COLORS = {"market_maker": BLUE, "momentum": ORANGE}

DIVERGING = LinearSegmentedColormap.from_list(
    "pnl", ["#9e2b2b", "#e34948", "#f2b8b5", "#f0efec", "#b7d3f6", "#2a78d6", "#184f95"]
)


def apply_style() -> None:
    plt.rcParams.update({
        "figure.facecolor": SURFACE,
        "axes.facecolor": SURFACE,
        "savefig.facecolor": SURFACE,
        "axes.edgecolor": AXIS,
        "axes.labelcolor": INK_2,
        "axes.titlecolor": INK,
        "axes.titlesize": 12,
        "axes.titleweight": "bold",
        "axes.titlelocation": "left",
        "axes.titlepad": 10,
        "axes.labelsize": 10,
        "axes.grid": True,
        "axes.axisbelow": True,
        "axes.spines.top": False,
        "axes.spines.right": False,
        "grid.color": GRID,
        "grid.linewidth": 0.8,
        "xtick.color": MUTED,
        "ytick.color": MUTED,
        "xtick.labelcolor": INK_2,
        "ytick.labelcolor": INK_2,
        "xtick.labelsize": 9,
        "ytick.labelsize": 9,
        "lines.linewidth": 2.0,
        "legend.frameon": False,
        "legend.fontsize": 9,
        "legend.labelcolor": INK_2,
        "font.family": ["DejaVu Sans"],
        "figure.dpi": 110,
        "savefig.dpi": 150,
        "savefig.bbox": "tight",
    })


apply_style()

_money = FuncFormatter(lambda v, _: f"-${abs(v):,.0f}" if v < 0 else f"${v:,.0f}")


def _color_for(label: str, i: int) -> str:
    for key, c in STRATEGY_COLORS.items():
        if key in label:
            return c
    return [BLUE, ORANGE, AQUA, VIOLET][i % 4]


def _zero_line(ax: plt.Axes, horizontal: bool = True) -> None:
    (ax.axhline if horizontal else ax.axvline)(0, color=AXIS, linewidth=1.0, zorder=1)


def pnl_over_time(results: Mapping[str, "object"], title: str = "Strategy PnL vs. price") -> plt.Figure:
    """Price on top, cumulative PnL (after fees) of each strategy below. Shared time axis, separate y-axes."""
    fig, (ax_px, ax_pnl) = plt.subplots(2, 1, figsize=(10, 6.2), sharex=True, height_ratios=[1, 1.6])
    first = next(iter(results.values()))
    px = first.equity["mark_price"]
    ax_px.plot(px.index, px.values, color=MUTED, linewidth=1.5)
    ax_px.set_title("BTC price (last trade)")
    ax_px.yaxis.set_major_formatter(_money)

    for i, (label, r) in enumerate(results.items()):
        pnl = r.equity["equity"] - r.metrics["initial_cash"]
        c = _color_for(r.strategy, i)
        ax_pnl.plot(pnl.index, pnl.values, color=c, label=label)
        ax_pnl.annotate(f"{label}  {pnl.iloc[-1]:+,.0f}", xy=(pnl.index[-1], pnl.iloc[-1]), xytext=(6, 0),
                        textcoords="offset points", va="center", fontsize=9, color=INK_2)
    _zero_line(ax_pnl)
    ax_pnl.set_title("Cumulative PnL after fees")
    ax_pnl.yaxis.set_major_formatter(_money)
    ax_pnl.legend(loc="upper left")
    fig.suptitle(title, x=0.01, ha="left", fontsize=14, fontweight="bold", color=INK)
    fig.autofmt_xdate()
    fig.tight_layout()
    return fig


def sweep_heatmap(df: pd.DataFrame, x: str, y: str, value: str = "total_pnl", agg: str = "max",
                  title: str | None = None, fmt: str = "{:,.0f}", ax: plt.Axes | None = None) -> plt.Figure:
    """Heatmap of a sweep metric over two parameters (other params aggregated with ``agg``).

    Diverging colour centred on zero: blue = profit, red = loss, grey = flat.
    """
    table = df.pivot_table(index=y, columns=x, values=value, aggfunc=agg).sort_index(ascending=False)
    fig = ax.figure if ax is not None else plt.figure(figsize=(1.1 * table.shape[1] + 2.6, 0.6 * table.shape[0] + 1.8))
    ax = ax or fig.add_subplot()
    v = table.to_numpy(dtype=float)
    lim = np.nanmax(np.abs(v)) or 1.0
    norm = TwoSlopeNorm(vmin=-lim, vcenter=0.0, vmax=lim)
    im = ax.imshow(v, cmap=DIVERGING, norm=norm, aspect="auto")
    ax.grid(False)
    for s in ax.spines.values():
        s.set_visible(False)
    ax.set_xticks(range(table.shape[1]), [f"{c:g}" for c in table.columns])
    ax.set_yticks(range(table.shape[0]), [f"{r:g}" for r in table.index])
    ax.set_xlabel(x)
    ax.set_ylabel(y)
    for (i, j), val in np.ndenumerate(v):
        if np.isnan(val):
            continue
        strong = abs(val) / lim > 0.55
        ax.text(j, i, fmt.format(val), ha="center", va="center", fontsize=8.5,
                color="#ffffff" if strong else INK)
    cb = fig.colorbar(im, ax=ax, fraction=0.04, pad=0.02)
    cb.outline.set_visible(False)
    cb.ax.tick_params(labelsize=8, colors=MUTED)
    cb.set_label(value, color=INK_2)
    ax.set_title(title or f"{value} by {y} × {x}" + (f" ({agg} over other params)" if agg else ""))
    fig.tight_layout()
    return fig


def sensitivity(df: pd.DataFrame, x: str, y: str = "total_pnl", label: str = "", color: str = BLUE,
                title: str = "", xlabel: str | None = None, ax: plt.Axes | None = None,
                mark_breakeven: bool = True) -> plt.Figure:
    """Line chart of one metric against one swept setting, with the breakeven crossing marked."""
    fig = ax.figure if ax is not None else plt.figure(figsize=(7, 4))
    ax = ax or fig.add_subplot()
    d = df.sort_values(x)
    ax.plot(d[x], d[y], color=color, marker="o", markersize=6, label=label or None)
    _zero_line(ax)
    if mark_breakeven:
        xs, ys = d[x].to_numpy(float), d[y].to_numpy(float)
        for k in range(len(xs) - 1):
            if ys[k] == 0 or (ys[k] > 0) != (ys[k + 1] > 0):
                xb = xs[k] - ys[k] * (xs[k + 1] - xs[k]) / (ys[k + 1] - ys[k])
                ax.axvline(xb, color=MUTED, linestyle=(0, (3, 3)), linewidth=1)
                ax.annotate(f"breakeven ≈ {xb:.2f}", xy=(xb, 0), xytext=(6, 10), textcoords="offset points",
                            fontsize=9, color=INK_2)
                break
    if y in ("total_pnl", "realized_pnl", "fees", "max_drawdown"):
        ax.yaxis.set_major_formatter(_money)
    ax.set_xlabel(xlabel or x)
    ax.set_ylabel(y)
    if title:
        ax.set_title(title)
    if label:
        ax.legend()
    fig.tight_layout()
    return fig


def in_vs_out_of_sample(splits: Mapping[str, "object"]) -> plt.Figure:
    """One panel per strategy: each dot is a parameter combo, in-sample vs out-of-sample score."""
    fig, axes = plt.subplots(1, len(splits), figsize=(5.2 * len(splits), 4.6), squeeze=False)
    for ax, (name, split) in zip(axes[0], splits.items()):
        j = split.joined()
        c = STRATEGY_COLORS.get(split.strategy, BLUE)
        ax.scatter(j["in_sample"], j["out_of_sample"], s=36, color=c, alpha=0.75, edgecolor=SURFACE, linewidth=1)
        best = split.best_row()
        ax.scatter([best["in_sample"]], [best["out_of_sample"]], s=110, facecolor="none", edgecolor=INK, linewidth=1.6)
        ax.annotate("selected in-sample", xy=(best["in_sample"], best["out_of_sample"]), xytext=(8, -14),
                    textcoords="offset points", fontsize=8.5, color=INK_2)
        _zero_line(ax)
        _zero_line(ax, horizontal=False)
        ax.xaxis.set_major_formatter(_money)
        ax.yaxis.set_major_formatter(_money)
        ax.set_xlabel(f"in-sample {split.metric}")
        ax.set_ylabel(f"out-of-sample {split.metric}")
        ax.set_title(f"{name}: rank corr {split.rank_correlation():+.2f}")
    fig.suptitle("Do in-sample winners stay winners?", x=0.01, ha="left", fontsize=14, fontweight="bold", color=INK)
    fig.tight_layout()
    return fig


def typical_window(fills: pd.DataFrame, minutes: float = 1.0, quantile: float = 0.6) -> pd.Timestamp:
    """Start of a window whose fill count is typical (not the busiest), for readable zooms."""
    counts = fills["qty"].resample(f"{minutes:g}min").count()
    counts = counts[counts > 0]
    target = counts.quantile(quantile)
    return (counts - target).abs().idxmin()


def mm_microstructure(trades: pd.DataFrame, result: "object", start: pd.Timestamp, minutes: float = 5.0) -> plt.Figure:
    """Zoomed view of the market maker: prints, its bid/ask quotes, its fills, and inventory."""
    end = start + pd.Timedelta(minutes=minutes)
    tr = trades.loc[start:end]
    sig = result.signals
    if sig is None:
        raise ValueError("run the backtest with record_signals=True")
    sig = sig.loc[start - pd.Timedelta(minutes=1):end]
    fills = result.fills.loc[start:end]

    fig, (ax, ax_inv) = plt.subplots(2, 1, figsize=(10, 6.2), sharex=True, height_ratios=[2.2, 1])
    ax.scatter(tr.index, tr["price"], s=4, color=MUTED, alpha=0.5, linewidth=0, label="market trades")
    for name, color, lab in (("bid", BLUE, "MM bid"), ("ask", ORANGE, "MM ask")):
        q = sig[sig["name"] == name]["value"].replace(0.0, np.nan)
        q = q[~q.index.duplicated(keep="last")]
        ax.step(q.index, q.values, where="post", color=color, linewidth=1.4, label=lab)
    buys, sells = fills[fills["side"] > 0], fills[fills["side"] < 0]
    ax.scatter(buys.index, buys["price"], marker="^", s=60, color=BLUE, edgecolor=INK, linewidth=0.6, zorder=5, label="MM buy fill")
    ax.scatter(sells.index, sells["price"], marker="v", s=60, color=ORANGE, edgecolor=INK, linewidth=0.6, zorder=5, label="MM sell fill")
    ax.set_xlim(start, end)
    lo, hi = tr["price"].quantile(0.002), tr["price"].quantile(0.998)
    pad = (hi - lo) * 0.25
    ax.set_ylim(lo - pad, hi + pad)
    ax.yaxis.set_major_formatter(FuncFormatter(lambda v, _: f"${v:,.1f}"))
    ax.set_title(f"Market maker quoting around the tape ({minutes:g} min)")
    ax.legend(loc="upper left", ncol=3)

    # Position after every fill over the whole run, then zoom to the window.
    all_pos = (result.fills["side"] * result.fills["qty"]).cumsum()
    before = all_pos.loc[:start]
    pos = all_pos.loc[start:end]
    if len(before):
        pos = pd.concat([pd.Series([before.iloc[-1]], index=[start]), pos])
    if len(pos):
        pos = pd.concat([pos, pd.Series([pos.iloc[-1]], index=[end])])
        ax_inv.step(pos.index, pos.values, where="post", color=BLUE)
    _zero_line(ax_inv)
    ax_inv.set_title("Inventory (BTC)")
    fig.autofmt_xdate()
    fig.tight_layout()
    return fig


def save(fig: plt.Figure, path: str) -> str:
    fig.savefig(path)
    plt.close(fig)
    return path
