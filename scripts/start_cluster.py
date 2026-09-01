#!/usr/bin/env python3
"""Start a local DSE cluster for development."""

import argparse
import os
import signal
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
BUILD = ROOT / "build"


def find_binary(name: str) -> Path:
    for candidate in [BUILD / name, BUILD / "Release" / name, BUILD / "Debug" / name]:
        if candidate.exists():
            return candidate
    print(f"Binary not found: {name}. Run: cmake -B build && cmake --build build")
    sys.exit(1)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--storage-nodes", type=int, default=3)
    args = parser.parse_args()

    procs = []
    base_port = 50053

    metadata = find_binary("dse-metadata-service")
    storage = find_binary("dse-storage-node")

    os.makedirs(ROOT / "data" / "metadata", exist_ok=True)

    print("Starting metadata service on :50051...")
    procs.append(subprocess.Popen(
        [str(metadata), "--address", "0.0.0.0:50051"],
        cwd=ROOT,
    ))
    time.sleep(2)

    for i in range(args.storage_nodes):
        node_id = f"storage{i + 1}"
        port = base_port + i
        storage_path = ROOT / "data" / node_id
        storage_path.mkdir(parents=True, exist_ok=True)

        env = os.environ.copy()
        env["DSE_METADATA_ADDRESS"] = "localhost:50051"

        print(f"Starting {node_id} on :{port}...")
        procs.append(subprocess.Popen(
            [str(storage),
             f"--node-id={node_id}",
             f"--address=0.0.0.0:{port}",
             f"--storage-path={storage_path}"],
            cwd=ROOT,
            env=env,
        ))

    print(f"\nCluster running with {args.storage_nodes} storage nodes.")
    print("Use: storage --metadata localhost:50051 put <file>")
    print("Press Ctrl+C to stop.\n")

    def shutdown(sig, frame):
        print("\nShutting down...")
        for p in procs:
            p.terminate()
        for p in procs:
            p.wait(timeout=5)
        sys.exit(0)

    signal.signal(signal.SIGINT, shutdown)
    signal.signal(signal.SIGTERM, shutdown)

    while True:
        time.sleep(1)


if __name__ == "__main__":
    main()
