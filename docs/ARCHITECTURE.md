# Distributed Storage Engine — Architecture

## 1. System Architecture

DSE follows a **metadata-centric architecture** similar to HDFS NameNode/DataNode or Ceph MON/OSD:

```
                    ┌─────────────┐
                    │   Client    │
                    │  (CLI/API)  │
                    └──────┬──────┘
                           │ gRPC
                    ┌──────▼──────┐
                    │  Metadata   │
                    │  Service    │
                    │ + Coordinator│
                    └──────┬──────┘
              ┌────────────┼────────────┐
              │            │            │
        ┌─────▼────┐ ┌────▼─────┐ ┌───▼──────┐
        │ Storage  │ │ Storage  │ │ Storage  │
        │  Node 1  │ │  Node 2  │ │  Node 3  │
        └──────────┘ └──────────┘ └──────────┘
              │            │            │
              └────────────┼────────────┘
                    Replication
```

### Design Principles

1. **Separation of metadata and data** — metadata service is the source of truth for object locations; storage nodes are dumb chunk stores
2. **Primary-backup replication** — client writes to primary, primary replicates to backups
3. **Configurable consistency** — majority or all-replica write acknowledgement
4. **Eventual replica consistency** — metadata is strongly consistent (single writer); replicas may lag briefly during recovery
5. **Fail-fast with repair** — heartbeat detects failures; background recovery repairs under-replicated chunks

## 2. Component Interaction

### Metadata Service

- Maintains in-memory object/chunk/node tables with JSON snapshot persistence
- Exposes gRPC APIs: CreateObject, LocateObject, UpdateChunk, RegisterNode, etc.
- Selects storage nodes using round-robin, least-used, or random strategies
- Tracks leader ID and generation number from coordinator

### Storage Node

- Persists chunks as `chunk_<uuid>.data` files under configurable path
- Serves WriteChunk, ReadChunk, DeleteChunk, ReplicateChunk, VerifyChunk RPCs
- Reports heartbeat to metadata with free space and chunk count
- Atomic chunk writes (temp file + rename)

### Cluster Coordinator

- Embedded in metadata service process
- Bully algorithm: highest-priority node becomes leader
- Leader triggers recovery on node failure
- Generation counter prevents stale leader actions

### Replication Manager

- Selects N nodes from metadata
- Writes to primary, replicates to secondaries
- Enforces quorum based on write_ack policy

### Recovery Manager

- Background loop scans for under-replicated chunks
- Reads from any live replica
- Re-replicates to healthy nodes
- Updates metadata with new replica locations

## 3. Storage Format Specification

### Object Metadata

```
ObjectID:     UUID
FileName:     string (unique)
Size:         uint64
ReplFactor:   uint32 (default 3)
Version:      uint32
Checksum:     SHA-256 of full object (optional)
Owner:        string
CreatedAt:    timestamp ms
UpdatedAt:    timestamp ms
Chunks:       []ChunkMetadata
```

### Chunk Metadata

```
ChunkID:      UUID
ObjectID:     UUID
ChunkIndex:   uint32
Size:         uint64
Checksum:     SHA-256 of chunk payload
Version:      uint32
Replicas:     []ReplicaLocation
```

### On-Disk Chunk File

Binary format with magic header `DSEK` (0x4445534B), version field, variable-length IDs, checksum, and raw payload. See `include/chunk/chunk_format.hpp`.

## 4. Network Protocol Overview

### Storage Service (storage.proto)

| RPC | Direction | Purpose |
|-----|-----------|---------|
| WriteChunk | Client/Primary → Node | Store chunk with header |
| ReadChunk | Client → Node | Read full or ranged chunk |
| DeleteChunk | Client → Node | Remove chunk file |
| ReplicateChunk | Primary → Secondary | Copy chunk to replica |
| Heartbeat | Node → Node | Local health check |
| VerifyChunk | Any → Node | Checksum validation |
| GetNodeStatus | Any → Node | Capacity and chunk count |

### Metadata Service (metadata.proto)

| RPC | Purpose |
|-----|---------|
| CreateObject | Plan chunks for new object |
| DeleteObject | Remove object metadata |
| LocateObject | Get object + chunk + replica info |
| ListObjects | Enumerate objects with prefix |
| UpdateChunk | Record replica locations post-write |
| RegisterNode | Add storage node to cluster |
| NodeHeartbeat | Update node liveness and capacity |
| GetClusterState | Full cluster view |
| SelectNodes | Load-balanced node selection |
| MarkNodeOffline | Failure detection action |
| GetUnderReplicatedChunks | Recovery input |

### Coordinator Service (coordinator.proto)

| RPC | Purpose |
|-----|---------|
| RequestElection | Bully election message |
| AnnounceLeader | Leader notification |
| CoordinatorHeartbeat | Leader liveness |
| TriggerRecovery | Initiate recovery for failed node |
| GetLeader | Query current leader |

## 5. Failure Recovery Design

### Detection

- Storage nodes send heartbeat every N seconds (default 5)
- Metadata marks node offline if heartbeat age exceeds timeout (default 15s)
- Coordinator can trigger explicit recovery via TriggerRecovery RPC

### Recovery Steps

1. Mark failed node offline in metadata
2. Query all chunks with fewer live replicas than replication factor
3. For each under-replicated chunk:
   - Read data from any surviving replica
   - Select new target nodes via load balancer
   - Replicate chunk to new nodes
   - Update metadata with new replica set and incremented version
4. Background recovery loop repeats every 30 seconds

### Edge Cases Handled

| Scenario | Handling |
|----------|----------|
| Node crash during write | Quorum check fails; client receives error; partial chunks cleaned on retry |
| Node crash during replication | Primary may succeed with majority; recovery fills missing replica |
| Checksum mismatch on read | Client tries next replica in list |
| Duplicate filename | CreateObject rejects with error |
| Empty file | Single zero-size chunk created |
| Disk full | WriteChunk fails; client notified |
| Concurrent delete and read | Metadata delete removes object; read fails at LocateObject |
| Stale replica | Version field on chunks; recovery increments version |

## 6. Consistency Model

- **Metadata**: strong consistency (single metadata service, reader-writer locks)
- **Replicas**: eventual consistency during failure/recovery windows
- **Write ACK**: configurable majority (⌊N/2⌋+1) or all replicas

## 7. Concurrency

- `ReadWriteLock` on metadata store for concurrent reads
- `ThreadPool` in client for parallel chunk uploads
- Per-chunk mutex in chunk store
- gRPC handles concurrent RPCs per service

## 8. Tradeoffs

| Decision | Rationale |
|----------|-----------|
| Bully over Raft | Simpler for 3-node demo clusters; adequate for prototype |
| In-memory metadata | Fast development; JSON persistence for restart survival; RocksDB optional |
| gRPC over custom TCP | Production-standard RPC with codegen, streaming, and tooling |
| Primary-backup replication | Industry standard (HDFS, GFS); simpler than chain replication |
| Embedded coordinator | Reduces deployment complexity for local clusters |
