#!/usr/bin/env python3
"""
Script to read JSON from stdin and plot the distribution of the
parsed benchmark results.

This script was primarily written by Gemini.
"""

import sys
import json
import pandas as pd
import seaborn as sns
import matplotlib.pyplot as plt


def main():
    # 1. Load data from stdin
    try:
        input_data = json.load(sys.stdin)
    except json.JSONDecodeError:
        print("Error: Input is not valid JSON.", file=sys.stderr)
        return

    # 2. Flatten JSON into a Tidy DataFrame
    # Target structure: [Implementation, Test Name, Measurement]
    records = []
    for entry in input_data:
        impl_name = entry.get("name", "Unknown")
        for result in entry.get("results", []):
            test_name = result.get("name", "Unknown")
            data_points = result.get("data", [])
            for value in data_points:
                records.append(
                    {"Implementation": impl_name, "Test": test_name, "Value": value}
                )

    if not records:
        print("No data found to plot.", file=sys.stderr)
        return

    df = pd.DataFrame(records)

    # 3. Create a graph for each unique test result
    unique_tests = df["Test"].unique()

    for test in unique_tests:
        test_df = df[df["Test"] == test]

        plt.figure(figsize=(10, 6))
        sns.set_theme(style="whitegrid")

        # Plotting both histogram and KDE (Kernel Density Estimate)
        # 'hue' maps the outermost "name" to a specific color
        sns.histplot(
            data=test_df,
            x="Value",
            hue="Implementation",
            kde=True,
            element="step",
            palette="viridis",
            alpha=0.4,
        )

        plt.title(f"Distribution Comparison: {test}", fontsize=15)
        plt.xlabel("Execution Time / Metric Value", fontsize=12)
        plt.ylabel("Frequency", fontsize=12)

        # Use a tight layout to prevent label clipping
        plt.tight_layout()

        plt.show()


if __name__ == "__main__":
    main()
