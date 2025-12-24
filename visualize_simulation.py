#!/usr/bin/env python3
"""
Visualization for memory allocator benchmarks.

Expected CSV (benchmark_results.csv by default) columns:
    algorithm, avg_allocation_time, avg_deallocation_time,
    memory_efficiency, internal_fragmentation,
    failed_allocations, total_time

If the CSV is absent, synthetic sample data will be generated.
"""

from __future__ import annotations

import csv
import os
from dataclasses import dataclass
from typing import List, Optional

import matplotlib.pyplot as plt
import numpy as np


@dataclass
class BenchmarkRow:
    algorithm: str
    avg_allocation_time: float
    avg_deallocation_time: float
    memory_efficiency: float        # percentage
    internal_fragmentation: float   # bytes
    failed_allocations: float
    total_time: float


def load_benchmark_csv(path: str = "benchmark_results.csv") -> Optional[List[BenchmarkRow]]:
    if not os.path.exists(path):
        return None

    rows: List[BenchmarkRow] = []
    try:
        with open(path, newline="") as f:
            reader = csv.DictReader(f)
            for r in reader:
                rows.append(
                    BenchmarkRow(
                        algorithm=r["algorithm"],
                        avg_allocation_time=float(r["avg_allocation_time"]),
                        avg_deallocation_time=float(r["avg_deallocation_time"]),
                        memory_efficiency=float(r["memory_efficiency"]),
                        internal_fragmentation=float(r["internal_fragmentation"]),
                        failed_allocations=float(r["failed_allocations"]),
                        total_time=float(r["total_time"]),
                    )
                )
    except Exception as e:
        print(f"Failed to read {path}: {e}")
        return None

    return rows or None


def synthetic_data() -> List[BenchmarkRow]:
    print("benchmark_results.csv not found — using synthetic sample data.")
    return [
        BenchmarkRow(
            algorithm="McKusick-Karels",
            avg_allocation_time=0.00042,
            avg_deallocation_time=0.00018,
            memory_efficiency=92.5,
            internal_fragmentation=18_000,
            failed_allocations=5,
            total_time=0.61,
        ),
        BenchmarkRow(
            algorithm="Power-of-2 (Buddy)",
            avg_allocation_time=0.00035,
            avg_deallocation_time=0.00022,
            memory_efficiency=88.1,
            internal_fragmentation=26_000,
            failed_allocations=9,
            total_time=0.57,
        ),
    ]


def _bar(ax, labels, values, title, ylabel, color="#4e79a7", yfmt="{:.3f}", horiz=False):
    if horiz:
        ax.barh(labels, values, color=color, alpha=0.9)
        for i, v in enumerate(values):
            ax.text(v, i, " " + yfmt.format(v), va="center", fontsize=9)
    else:
        ax.bar(labels, values, color=color, alpha=0.9)
        for i, v in enumerate(values):
            ax.text(i, v, "\n" + yfmt.format(v), ha="center", va="bottom", fontsize=9)
    ax.set_title(title, fontsize=11, fontweight="bold")
    ax.set_ylabel(ylabel)
    ax.grid(True, axis="y", alpha=0.25)


def plot_comparison(rows: List[BenchmarkRow]):
    algorithms = [r.algorithm for r in rows]
    x = np.arange(len(algorithms))

    alloc = [r.avg_allocation_time for r in rows]
    dealloc = [r.avg_deallocation_time for r in rows]
    eff = [r.memory_efficiency for r in rows]
    frag = [r.internal_fragmentation for r in rows]
    failed = [r.failed_allocations for r in rows]
    total = [r.total_time for r in rows]

    fig, axs = plt.subplots(2, 3, figsize=(14, 8))
    plt.suptitle("Memory Allocators Benchmark Comparison", fontsize=14, fontweight="bold")

    axs[0, 0].bar(x - 0.15, alloc, width=0.3, label="Alloc", color="#4e79a7")
    axs[0, 0].bar(x + 0.15, dealloc, width=0.3, label="Dealloc", color="#f28e2b")
    axs[0, 0].set_xticks(x)
    axs[0, 0].set_xticklabels(algorithms, rotation=10)
    axs[0, 0].set_ylabel("Seconds")
    axs[0, 0].set_title("Average Times", fontweight="bold")
    axs[0, 0].legend()
    axs[0, 0].grid(True, axis="y", alpha=0.25)

    _bar(axs[0, 1], algorithms, eff, "Memory Efficiency (%)", "Percent", "#59a14f", "{:.1f}")
    _bar(axs[0, 2], algorithms, frag, "Internal Fragmentation (bytes)", "Bytes", "#e15759", "{:,.0f}")
    _bar(axs[1, 0], algorithms, failed, "Failed Allocations", "Count", "#b07aa1", "{:.0f}")
    _bar(axs[1, 1], algorithms, total, "Total Benchmark Time (s)", "Seconds", "#edc948", "{:.3f}")

    axs[1, 2].axis("off")
    summary = build_summary(rows)
    axs[1, 2].text(
        0, 1, summary, va="top", ha="left", fontsize=10,
        family="monospace", transform=axs[1, 2].transAxes
    )

    plt.tight_layout(rect=[0, 0, 1, 0.95])
    plt.show()


def build_summary(rows: List[BenchmarkRow]) -> str:
    if len(rows) < 2:
        return "Provide at least two algorithms to compare."

    fastest = min(rows, key=lambda r: r.total_time).algorithm
    most_efficient = max(rows, key=lambda r: r.memory_efficiency).algorithm
    least_fragmented = min(rows, key=lambda r: r.internal_fragmentation).algorithm

    lines = [
        "Summary:",
        f"- Fastest total time: {fastest}",
        f"- Highest memory efficiency: {most_efficient}",
        f"- Lowest internal fragmentation: {least_fragmented}",
        "",
        "Tip: Export C benchmark metrics to benchmark_results.csv to use real data."
    ]
    return "\n".join(lines)


def main():
    rows = load_benchmark_csv()
    if rows is None:
        rows = synthetic_data()

    if not rows:
        print("No benchmark data available.")
        return

    plot_comparison(rows)


if __name__ == "__main__":
    main()