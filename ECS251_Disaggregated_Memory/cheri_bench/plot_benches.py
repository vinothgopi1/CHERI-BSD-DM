import pandas as pd
import matplotlib.pyplot as plt

def savefig(name):
    plt.tight_layout()
    plt.savefig(name, dpi=180)
    plt.close()

# --- ptrchase ---
ptr = pd.read_csv("ptrchase_sweep.csv")

# remove header rows accidentally included as data
ptr = ptr[ptr["bench"] == "ptrchase"]
ptr["N"] = ptr["N"].astype(int)
ptr["ns_per_op"] = ptr["ns_per_op"].astype(float)
plt.figure()
plt.plot(ptr["N"], ptr["ns_per_op"], marker="o")
plt.xscale("log")
plt.xlabel("Working set size (nodes)")
plt.ylabel("Latency per pointer dereference (ns)")
plt.title("Pointer-chasing latency (CHERI purecap)")
savefig("ptrchase.png")

# --- mallocbench ---
mal = pd.read_csv("malloc_sweep.csv")
malloc = mal[mal["name"]=="malloc"].copy()
free = mal[mal["name"]=="free"].copy()

plt.figure()
plt.plot(malloc["size"], malloc["ns_per_op"], marker="o", label="malloc")
plt.plot(free["size"], free["ns_per_op"], marker="o", label="free")
plt.xscale("log")
plt.xlabel("Allocation size (bytes, log scale)")
plt.ylabel("ns per op")
plt.title("malloc/free cost vs size (purecap)")
plt.legend()
savefig("malloc.png")

# --- rpcbench ---
rpc = pd.read_csv("rpc_sweep.csv")
rpc = rpc[rpc["bench"] == "rpcbench"]
rpc["len"] = pd.to_numeric(rpc["len"])
rpc["batch"] = pd.to_numeric(rpc["batch"])
rpc["MiB_per_s"] = pd.to_numeric(rpc["MiB_per_s"])
rpc["ns_per_op"] = pd.to_numeric(rpc["ns_per_op"])

plt.figure()
for b in sorted(rpc["batch"].unique()):
    sub = rpc[rpc["batch"]==b].sort_values("len")
    plt.plot(sub["len"], sub["ns_per_op"], marker="o", label=f"batch={b}")
plt.xscale("log")
plt.yscale("log")
plt.xlabel("Payload length (bytes, log scale)")
plt.ylabel("ns per op (log scale)")
plt.title("RPC write latency vs payload, batching effect")
plt.legend()
savefig("rpc_latency.png")

plt.figure()
for b in sorted(rpc["batch"].unique()):
    sub = rpc[rpc["batch"]==b].sort_values("len")
    plt.plot(sub["len"], sub["MiB_per_s"], marker="o", label=f"batch={b}")
plt.xscale("log")
plt.xlabel("Payload length (bytes, log scale)")
plt.ylabel("MiB/s")
plt.title("RPC write throughput vs payload, batching effect")
plt.legend()
savefig("rpc_throughput.png")

print("Wrote: ptrchase.png malloc.png rpc_latency.png rpc_throughput.png")