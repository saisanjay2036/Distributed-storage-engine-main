#!/usr/bin/env python3
"""Benchmark suite for Distributed Storage Engine."""

import argparse
import os
import random
import statistics
import subprocess
import sys
import tempfile
import time
from pathlib import Path


def run_cli(metadata_addr: str, args: list[str], timeout: int = 300) -> tuple[bool, float, str]:
    cmd = ["storage", "--metadata", metadata_addr] + args
    start = time.perf_counter()
    try:
        result = subprocess.run(cmd, capture_output=True, text=True, timeout=timeout)
        elapsed = time.perf_counter() - start
        ok = result.returncode == 0
        output = result.stdout + result.stderr
        return ok, elapsed, output
    except subprocess.TimeoutExpired:
        return False, timeout, "timeout"
    except FileNotFoundError:
        return False, 0, "storage CLI not found - build project first"


def generate_file(path: Path, size_mb: int) -> None:
    chunk = os.urandom(1024 * 1024)
    with open(path, "wb") as f:
        for _ in range(size_mb):
            f.write(chunk)


def bench_upload(metadata: str, size_mb: int, iterations: int) -> dict:
    latencies = []
    throughputs = []

    with tempfile.TemporaryDirectory() as tmpdir:
        for i in range(iterations):
            fpath = Path(tmpdir) / f"bench_{size_mb}mb_{i}.bin"
            generate_file(fpath, size_mb)
            ok, elapsed, _ = run_cli(metadata, ["put", str(fpath)])
            if ok:
                latencies.append(elapsed)
                throughputs.append(size_mb / elapsed)

    return {
        "size_mb": size_mb,
        "iterations": iterations,
        "success": len(latencies),
        "avg_latency_s": statistics.mean(latencies) if latencies else 0,
        "p95_latency_s": sorted(latencies)[int(len(latencies) * 0.95)] if latencies else 0,
        "avg_throughput_mbps": statistics.mean(throughputs) if throughputs else 0,
    }


def bench_download(metadata: str, filename: str, iterations: int) -> dict:
    latencies = []
    with tempfile.TemporaryDirectory() as tmpdir:
        for i in range(iterations):
            out = Path(tmpdir) / f"out_{i}.bin"
            ok, elapsed, _ = run_cli(metadata, ["get", filename, str(out)])
            if ok:
                latencies.append(elapsed)

    return {
        "filename": filename,
        "iterations": iterations,
        "success": len(latencies),
        "avg_latency_s": statistics.mean(latencies) if latencies else 0,
        "p95_latency_s": sorted(latencies)[int(len(latencies) * 0.95)] if latencies else 0,
    }


def bench_concurrent(metadata: str, size_mb: int, clients: int) -> dict:
    import concurrent.futures

    def upload_one(idx: int) -> float:
        with tempfile.TemporaryDirectory() as tmpdir:
            fpath = Path(tmpdir) / f"concurrent_{idx}.bin"
            generate_file(fpath, size_mb)
            ok, elapsed, _ = run_cli(metadata, ["put", str(fpath)])
            return elapsed if ok else -1

    start = time.perf_counter()
    with concurrent.futures.ThreadPoolExecutor(max_workers=clients) as pool:
        results = list(pool.map(upload_one, range(clients)))
    total = time.perf_counter() - start
    successes = [r for r in results if r >= 0]

    return {
        "clients": clients,
        "size_mb": size_mb,
        "success": len(successes),
        "total_time_s": total,
        "aggregate_throughput_mbps": (clients * size_mb) / total if total > 0 else 0,
    }


def main():
    parser = argparse.ArgumentParser(description="DSE Benchmark Suite")
    parser.add_argument("--metadata", default="localhost:50051")
    parser.add_argument("--dataset", choices=["100mb", "1gb", "10gb", "all"], default="100mb")
    parser.add_argument("--iterations", type=int, default=3)
    parser.add_argument("--concurrent", type=int, default=4)
    args = parser.parse_args()

    datasets = {"100mb": 100, "1gb": 1024, "10gb": 10240}
    sizes = list(datasets.values()) if args.dataset == "all" else [datasets[args.dataset]]

    print(f"Distributed Storage Engine Benchmark")
    print(f"Metadata: {args.metadata}")
    print("=" * 60)

    all_results = []

    for size_mb in sizes:
        print(f"\n--- Upload benchmark: {size_mb} MB ---")
        result = bench_upload(args.metadata, size_mb, args.iterations)
        all_results.append(result)
        print(f"  Success: {result['success']}/{result['iterations']}")
        print(f"  Avg latency: {result['avg_latency_s']:.2f}s")
        print(f"  P95 latency: {result['p95_latency_s']:.2f}s")
        print(f"  Avg throughput: {result['avg_throughput_mbps']:.2f} MB/s")

    print(f"\n--- Concurrent upload: {args.concurrent} clients, 10 MB each ---")
    conc = bench_concurrent(args.metadata, 10, args.concurrent)
    print(f"  Success: {conc['success']}/{conc['clients']}")
    print(f"  Total time: {conc['total_time_s']:.2f}s")
    print(f"  Aggregate throughput: {conc['aggregate_throughput_mbps']:.2f} MB/s")

    print("\n" + "=" * 60)
    print("Benchmark complete.")


if __name__ == "__main__":
    main()
