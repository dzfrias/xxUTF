#!/usr/bin/env python3
"""
Script to read JSON from stdin and plot the parsed benchmark results.

This script was primarily written by Gemini.
"""

import sys
import json
import numpy as np
import matplotlib.pyplot as plt


def main():
    # 1. Load data from stdin
    try:
        data = json.load(sys.stdin)
    except json.JSONDecodeError:
        print("Error: Invalid JSON input.")
        return

    if not data:
        print("Error: Empty data.")
        return

    # 2. Extract unique result names (the X-axis categories)
    # We assume all implementations have the same result categories
    result_names = [r["name"] for r in data[0]["results"]]
    num_categories = len(result_names)
    num_implementations = len(data)

    # 3. Setup Plotting dimensions
    x = np.arange(num_categories)  # Label locations
    width = 0.5 / num_implementations  # Width of individual bars

    fig, ax = plt.subplots(figsize=(10, 6))

    # 4. Plot bars for each implementation
    for i, implementation in enumerate(data):
        impl_name = implementation["name"]
        means = [r["mean_ms"] for r in implementation["results"]]
        stds = [r["sd_ms"] for r in implementation["results"]]

        # Calculate position offset for grouping
        offset = (i - (num_implementations - 1) / 2) * width

        ax.bar(x + offset, means, width, label=impl_name, yerr=stds, capsize=5)

    # 5. Styling
    ax.set_ylabel("Execution Time (ms)")
    ax.set_title("Benchmark Results by Implementation")
    ax.set_xticks(x)
    ax.set_xticklabels(result_names)
    ax.legend()
    ax.grid(axis="y", linestyle="--", alpha=0.7)

    plt.tight_layout()
    plt.show()


if __name__ == "__main__":
    main()
