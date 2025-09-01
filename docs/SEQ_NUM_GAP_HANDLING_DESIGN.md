# Sequence Number Gap Handling Design

## Overview

Implement a non-blocking sequence number gap detection and recovery system that maintains high-performance message processing while ensuring FIX protocol sequence integrity.

## Architecture Principles

- **Non-blocking**: Main parser continues processing valid messages regardless of gaps
- **Parallel Processing**: Gap detection/resolution runs on dedicated CPU core
- **Graceful Escalation**: Multiple recovery levels from simple resend to session reset
- **Performance First**: Maintain sub-microsecond latency targets

## Implementation Steps

### Step 1: Core Data Structures

**1.1 Gap Queue Structure**
- Lock-free queue for gap sequence numbers
- Entry structure: `{seq_number, timestamp, retry_count, timeout}`
- Initial capacity: 1024 entries
- Location: `include/manager/sequence_gap_manager.h`

**1.2 Expected Sequence Tracking**
- Thread-safe expected sequence number counter
- Per-session tracking (if multiple sessions supported)
- Atomic operations for thread safety

### Step 2: Parser Integration

**2.1 Sequence Validation in StreamFixParser**
- Check incoming sequence vs expected
- If sequence matches expected: process normally, increment expected
- If sequence > expected: add gap(s) to gap queue, process current message, update expected to current+1
- If sequence < expected: handle duplicate/replay logic

**2.2 Gap Queue Integration**
- Add gap entries when sequence jump detected
- Non-blocking gap queue insertion
- Location: Modify `src/protocol/stream_fix_parser.cpp`

### Step 3: Dedicated Gap Management Thread

**3.1 Gap Manager Component**
- Dedicated thread pinned to specific CPU core
- Monitors gap queue continuously
- Manages resend requests and escalation logic
- Location: `src/manager/sequence_gap_manager.cpp`

**3.2 Thread Configuration**
- CPU pinning using `pthread_setaffinity_np`
- High priority thread scheduling
- Configurable polling interval (suggest 1ms)

### Step 4: Resend Request Logic

**4.1 ResendRequest Message Generation**
- Create FIX ResendRequest messages for gaps
- Route through HIGH priority outbound queue
- Include BeginSeqNo and EndSeqNo fields

**4.2 Retry Strategy**
- Exponential backoff: 100ms, 500ms, 1s, 2s, 5s
- Maximum 5 retries before escalation
- Update retry_count and timestamp in gap entries

### Step 5: Escalation Framework

**5.1 Level 1 - Normal Gap Handling**
- Send ResendRequest via HIGH priority queue
- Retry with exponential backoff
- Log gap detection events

**5.2 Level 2 - Queue Size Threshold**
- Warning threshold: 50 gaps
- Critical threshold: 200 gaps
- Actions: Increase resend frequency, alert monitoring

**5.3 Level 3 - Timeout Handling**
- Per-gap timeout: 10 seconds
- Session timeout: 30 seconds for any unresolved gap
- Actions: Prepare for sequence reset

**5.4 Level 4 - Emergency Recovery**
- Send SequenceReset message (GapFill=Y)
- Alternative: Disconnect/reconnect session
- Alert operations team
- Log emergency recovery events

### Step 6: Gap Resolution

**6.1 Gap Fulfillment**
- When expected gap sequence arrives, remove from gap queue
- Process the gap-filling message normally
- Continue with next expected sequence

**6.2 Queue Cleanup**
- Remove fulfilled gaps from queue
- Periodic cleanup of expired entries
- Memory management for gap entries

### Step 7: Configuration

**7.1 Configurable Parameters**
```
gap_queue_size: 1024
gap_timeout_seconds: 10
session_timeout_seconds: 30
warning_threshold: 50
critical_threshold: 200
max_retries: 5
retry_intervals: [100, 500, 1000, 2000, 5000] // milliseconds
cpu_core_affinity: 3 // dedicated core for gap manager
polling_interval_ms: 1
```

**7.2 Runtime Configuration**
- Configuration file support
- Runtime parameter updates via management interface

### Step 8: Monitoring & Metrics

**8.1 Prometheus Metrics**
- `gap_queue_depth`: Current number of unresolved gaps
- `gap_resolution_time`: Average time to resolve gaps
- `resend_requests_sent`: Counter of resend messages
- `emergency_recoveries`: Counter of sequence resets/reconnects
- `gaps_detected`: Counter of sequence gaps found
- `gaps_resolved`: Counter of gaps successfully filled

**8.2 Logging**
- Gap detection events
- Resend request transmissions
- Escalation level changes
- Recovery actions taken

### Step 9: Testing Strategy

**9.1 Unit Tests**
- Gap queue operations
- Sequence validation logic
- Escalation threshold triggers
- Message generation correctness

**9.2 Integration Tests**
- End-to-end gap detection and resolution
- Multi-threaded safety verification
- Performance impact measurement
- Timeout handling validation

**9.3 Performance Tests**
- Latency impact of gap detection
- Throughput with various gap scenarios
- Memory usage under high gap volumes
- CPU utilization of gap manager thread

## File Structure

```
include/manager/
  └── sequence_gap_manager.h

src/manager/
  └── sequence_gap_manager.cpp

tests/
  └── test_sequence_gap_manager.cpp

docs/
  └── sequence_gap_handling_design.md (this file)
```

## Implementation Order

1. Create gap queue data structures
2. Integrate sequence validation in parser
3. Implement basic gap manager thread
4. Add resend request generation
5. Implement escalation logic
6. Add monitoring and configuration
7. Comprehensive testing
8. Performance optimization

## Success Criteria

- Main parsing latency impact < 100ns
- Gap detection accuracy: 100%
- Gap resolution time < 5 seconds (normal operation)
- System handles 10,000+ messages/sec with gaps
- Zero message loss during gap handling
- Graceful recovery from all failure scenarios