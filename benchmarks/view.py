#!/usr/bin/env python3
"""
Script to read numeric values from a file line-by-line and create a distribution plot.

This script was primarily written by Claude.
"""

import matplotlib.pyplot as plt
import numpy as np
import argparse
import sys
from pathlib import Path


def read_numeric_file(filename):
    """
    Read numeric values from a file, one per line.

    Args:
        filename (str): Path to the input file

    Returns:
        list: List of numeric values

    Raises:
        FileNotFoundError: If the file doesn't exist
        ValueError: If non-numeric values are encountered
    """
    values = []

    with open(filename, "r") as file:
        for line_num, line in enumerate(file, 1):
            line = line.strip()

            # Skip empty lines
            if not line:
                continue

            # Try to convert to float
            value = float(line)
            values.append(value)

    return values


def create_distribution_plot(values, filename, output_file=None):
    """
    Create and display a distribution plot of the values.

    Args:
        values (list): List of numeric values
        filename (str): Original filename for plot title
        output_file (str, optional): Path to save the plot
    """
    if not values:
        print("No numeric values found in the file.")
        return

    plt.figure(figsize=(10, 6))

    # Histogram
    plt.hist(values, bins=30, alpha=0.7, color="skyblue", edgecolor="black")
    plt.title(f"Distribution of Values\n({Path(filename).name})")
    plt.xlabel("Value")
    plt.ylabel("Frequency")
    plt.grid(True, alpha=0.3)

    plt.tight_layout()

    # Display statistics
    stats_text = f"""
    Statistics for {Path(filename).name}:
    Count: {len(values)}
    Mean: {np.mean(values):.2f}
    Median: {np.median(values):.2f}
    Std Dev: {np.std(values):.2f}
    Min: {min(values):.2f}
    Max: {max(values):.2f}
    """
    print(stats_text)

    # Save plot if output file specified
    if output_file:
        plt.savefig(output_file, dpi=300, bbox_inches="tight")
        print(f"Plot saved to: {output_file}")

    # Show plot
    plt.show()


def main():
    """Main function to handle command line arguments and execute the script."""
    parser = argparse.ArgumentParser(
        description="Read numeric values from a file and create distribution plots."
    )
    parser.add_argument(
        "filename", help="Path to the input file containing numeric values"
    )
    parser.add_argument(
        "-o", "--output", help="Output file path to save the plot (optional)"
    )

    args = parser.parse_args()

    # Check if file exists
    if not Path(args.filename).exists():
        print(f"Error: File '{args.filename}' does not exist.")
        sys.exit(1)

    # Read the file
    print(f"Reading values from: {args.filename}")
    values = read_numeric_file(args.filename)

    if not values:
        print("No valid numeric values found in the file.")
        sys.exit(1)

    print(f"Successfully read {len(values)} numeric values.")

    # Create the plot
    create_distribution_plot(values, args.filename, args.output)


if __name__ == "__main__":
    main()
