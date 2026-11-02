#!/usr/bin/env python3
"""Turn MemoryForge CSV output into charts and a Markdown summary.

Reads  <reports>/allocator/*.summary.csv, *.timeseries.csv and <reports>/virtual-memory/*.summary.csv
Writes PNG charts plus results.md into --out.  The script only plots what the CSV files contain;
it never invents or smooths data.

Usage:
    python analysis/plot_results.py --reports reports/local --out reports/local/charts
"""
import argparse
import sys
from pathlib import Path

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt  # noqa: E402
import pandas as pd  # noqa: E402

STRATEGY_ORDER = ["first-fit", "best-fit", "worst-fit"]
POLICY_ORDER = ["fifo", "lru", "clock", "opt"]
COLORS = {"first-fit": "#1f4e79", "best-fit": "#2a9d8f", "worst-fit": "#c0504d",
          "fifo": "#1f4e79", "lru": "#2a9d8f", "clock": "#8a6d3b", "opt": "#6b6b6b"}


def read_all(pattern_dir: Path, suffix: str) -> pd.DataFrame:
    frames = [pd.read_csv(p) for p in sorted(pattern_dir.glob(f"*{suffix}"))]
    return pd.concat(frames, ignore_index=True) if frames else pd.DataFrame()


def chart_latency(summary: pd.DataFrame, out: Path) -> None:
    """Chart A - allocation latency (median, p95, p99) per workload and strategy."""
    workloads = sorted(summary["workload_id"].unique())
    fig, axes = plt.subplots(1, len(workloads), figsize=(3.4 * len(workloads), 3.8), sharey=False, squeeze=False)
    for ax, wid in zip(axes[0], workloads):
        sub = summary[summary["workload_id"] == wid].set_index("strategy").reindex(STRATEGY_ORDER)
        x = range(len(sub))
        width = 0.26
        for i, (col, label) in enumerate([("alloc_latency_median_ns", "median"), ("alloc_latency_p95_ns", "p95"),
                                          ("alloc_latency_p99_ns", "p99")]):
            ax.bar([v + (i - 1) * width for v in x], sub[col], width, label=label, alpha=0.9 - 0.2 * i)
        ax.set_xticks(list(x))
        ax.set_xticklabels(STRATEGY_ORDER, rotation=20)
        ax.set_title(f"{wid} {sub['workload_name'].iloc[0]}", fontsize=9)
        ax.set_yscale("log")
        ax.grid(axis="y", alpha=0.3)
    axes[0][0].set_ylabel("allocate() latency (ns, log scale)")
    axes[0][-1].legend(fontsize=8)
    env = summary.iloc[0]
    fig.suptitle(f"Chart A - allocation latency  ({env['compiler']}, {env['build_type']})", fontsize=10)
    fig.tight_layout()
    fig.savefig(out / "chart_a_allocation_latency.png", dpi=150)
    plt.close(fig)


def chart_fragmentation(series: pd.DataFrame, out: Path) -> None:
    """Chart B - fragmentation indicator over time, one panel per workload."""
    workloads = sorted(series["workload_id"].unique())
    fig, axes = plt.subplots(1, len(workloads), figsize=(3.6 * len(workloads), 3.6), sharey=True, squeeze=False)
    for ax, wid in zip(axes[0], workloads):
        for strategy in STRATEGY_ORDER:
            s = series[(series["workload_id"] == wid) & (series["strategy"] == strategy)]
            # Gaps (no free bytes) stay gaps: an undefined indicator is not plotted as 0.
            ax.plot(s["op_index"], s["external_fragmentation"], label=strategy, color=COLORS[strategy], linewidth=1.2)
        ax.set_title(wid, fontsize=9)
        ax.set_xlabel("operations executed")
        ax.grid(alpha=0.3)
    axes[0][0].set_ylabel("1 - largest_free / total_free")
    axes[0][-1].legend(fontsize=8)
    fig.suptitle("Chart B - external fragmentation indicator over time", fontsize=10)
    fig.tight_layout()
    fig.savefig(out / "chart_b_fragmentation_over_time.png", dpi=150)
    plt.close(fig)


def chart_failures(summary: pd.DataFrame, out: Path) -> None:
    """Allocation failure rate per workload and strategy."""
    pivot = summary.pivot(index="workload_id", columns="strategy", values="failure_rate")[STRATEGY_ORDER] * 100
    ax = pivot.plot(kind="bar", figsize=(7, 3.8), color=[COLORS[s] for s in STRATEGY_ORDER])
    ax.set_ylabel("failed allocations (% of attempts)")
    ax.set_title("Allocation failure rate", fontsize=10)
    ax.grid(axis="y", alpha=0.3)
    plt.xticks(rotation=0)
    plt.tight_layout()
    plt.savefig(out / "chart_allocation_failure_rate.png", dpi=150)
    plt.close()


def chart_page_faults(vm: pd.DataFrame, out: Path) -> None:
    """Chart C - page faults by replacement policy, one panel per workload, x = frames."""
    workloads = sorted(vm["workload_id"].unique())
    fig, axes = plt.subplots(1, len(workloads), figsize=(3.4 * len(workloads), 3.6), squeeze=False)
    for ax, wid in zip(axes[0], workloads):
        sub = vm[vm["workload_id"] == wid]
        for policy in POLICY_ORDER:
            p = sub[sub["policy"] == policy].sort_values("frames")
            style = "--" if policy == "opt" else "-"
            ax.plot(p["frames"], p["page_faults"], style, marker="o", markersize=3.5, label=policy, color=COLORS[policy])
        ax.set_xscale("log", base=2)
        ax.set_title(f"{wid} {sub['workload_name'].iloc[0]}", fontsize=9)
        ax.set_xlabel("physical frames")
        ax.grid(alpha=0.3)
    axes[0][0].set_ylabel("page faults")
    axes[0][-1].legend(fontsize=8)
    fig.suptitle("Chart C - page faults by policy (OPT is an offline lower bound)", fontsize=10)
    fig.tight_layout()
    fig.savefig(out / "chart_c_page_faults_by_policy.png", dpi=150)
    plt.close(fig)


def write_markdown(allocator: pd.DataFrame, vm: pd.DataFrame, out: Path) -> None:
    lines = ["# Results summary", "",
             "Generated by `analysis/plot_results.py` from the CSV files; nothing here is hand-edited.", ""]
    if not allocator.empty:
        env = allocator.iloc[0]
        lines += ["## Allocator", "",
                  f"Build: {env['compiler']}, {env['build_type']}, git `{env['git_commit']}`. "
                  "Fragmentation, failure and search-step columns are deterministic; latency/throughput are wall-clock.",
                  ""]
        cols = ["workload_id", "strategy", "failure_rate", "final_external_fragmentation", "search_steps_per_attempt",
                "alloc_latency_median_ns", "alloc_latency_p99_ns", "throughput_median_ops_per_s"]
        table = allocator[cols].copy()
        table["failure_rate"] = (table["failure_rate"] * 100).round(2).astype(str) + "%"
        table["final_external_fragmentation"] = table["final_external_fragmentation"].round(3)
        table["search_steps_per_attempt"] = table["search_steps_per_attempt"].round(1)
        table["throughput_median_ops_per_s"] = (table["throughput_median_ops_per_s"] / 1e6).round(2)
        table = table.rename(columns={"throughput_median_ops_per_s": "Mops/s"})
        lines += [table.to_markdown(index=False) if hasattr(table, "to_markdown") and _has_tabulate() else table.to_string(index=False), ""]
    if not vm.empty:
        lines += ["## Virtual memory (page faults)", ""]
        pivot = vm.pivot_table(index=["workload_id", "frames"], columns="policy", values="page_faults")
        pivot = pivot[[p for p in POLICY_ORDER if p in pivot.columns]]
        lines += [pivot.to_markdown() if _has_tabulate() else pivot.to_string(), ""]
    (out / "results.md").write_text("\n".join(lines), encoding="utf-8")


def _has_tabulate() -> bool:
    try:
        import tabulate  # noqa: F401
        return True
    except ImportError:
        return False


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--reports", default="reports/local", type=Path)
    parser.add_argument("--out", default=None, type=Path)
    args = parser.parse_args()
    out = args.out or args.reports / "charts"
    out.mkdir(parents=True, exist_ok=True)

    allocator = read_all(args.reports / "allocator", ".summary.csv")
    series = read_all(args.reports / "allocator", ".timeseries.csv")
    vm = read_all(args.reports / "virtual-memory", ".summary.csv")
    if allocator.empty and vm.empty:
        print(f"no CSV results found under {args.reports}", file=sys.stderr)
        return 1

    if not allocator.empty:
        chart_latency(allocator, out)
        chart_failures(allocator, out)
    if not series.empty:
        chart_fragmentation(series, out)
    if not vm.empty:
        chart_page_faults(vm, out)
    write_markdown(allocator, vm, out)
    print(f"charts and results.md written to {out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
