# Sakura Architecture Implementation Plan

This plan turns the revised architecture into an implementation sequence that
fits the current repository. It intentionally keeps the protocol and source
tree compact. The goal is to make the local desktop/agent relationship
correct first, then add one small remote gateway.

## Starting point

The repository already has the main local building blocks:

- `proto/sakura/control.proto` is the current Protobuf contract.
- `sakura-control-transport.*` implements framed Protobuf over a Unix socket.
- `sakura-control-client.*` is the desktop-side local client.
- `sakura-agent.c` owns the agent process, workspace runtime, terminals, and
  PTYs.
- `sakura-core.*` contains GTK-free workspace and session model code.

This plan extends those pieces. It does not propose a tree-wide source move or
a replacement transport.

## Design rules

1. The agent is authoritative for workspace and terminal runtime state.
2. The desktop is a projection. Its GTK layout and visual focus are local UI
   state, not workspace authority.
3. Keep one `proto/sakura/control.proto` file for now. Organize it with clear
   sections and comments; split it only when the file becomes a real build or
   ownership problem.
4. Keep the current flat `src/` layout. Add a new file only for a cohesive
   responsibility, not for every message or helper.
5. The agent accepts only local IPC. Network connections belong to the future
   gateway.
6. Use fresh snapshots for recovery. Do not add durable event sourcing or a
   terminal-output database in this work.

## Ownership model

| State | Owner |
| --- | --- |
| Groups, tasks, pages, and terminal identities | Agent |
| Terminal processes, PTYs, and runtime status | Agent |
| Workspace revision | Agent |
| GTK widgets, pane geometry, and split layout | Desktop |
| Visual focus and local navigation | Desktop |
| Remote authentication and device permissions | Gateway |

Every new mutable field must be assigned to one owner before it is added.

## Compact target shape

Keep the current source layout through the local milestones:

```text
proto/sakura/control.proto
src/
  sakura-agent.c
  sakura-control-client.*
  sakura-control-transport.*
  sakura-core*.*
  existing desktop modules
```

Only when remote work starts, add a small isolated gateway area:

```text
gateway/              # one executable/package initially
clients/web/          # after the gateway is usable
clients/android/      # after the web protocol is proven
```

The gateway may grow into a few cohesive files later, but it should begin as
one small executable rather than a framework or a collection of transport
abstractions.

## Milestone 1: make the local architecture reliable

### 1. Make reconnect snapshot-first

Change the desktop reconnect sequence to:

```text
connect → handshake → get snapshot → apply projection → subscribe → mutate
```

Snapshot application must update the desktop model without sending commands
back to the agent. Stale desktop hierarchy and layout references are discarded
or mapped to safe defaults. Surviving agent terminals must not be respawned.

Likely implementation area: the existing agent client, workspace projection,
and control protocol handling. Do not create a separate synchronization
subsystem.

Acceptance:

- Agent state wins after a desktop restart.
- Surviving terminals continue running.
- Applying a snapshot produces no mutation requests.
- Missing local layout references fall back safely.

### 2. Add a workspace revision

Keep the existing event `sequence` for transport/event ordering and add a
separate `workspace_revision` for domain mutations.

Add the revision to snapshots, successful mutation responses, and workspace
change events. Add an optional expected revision to mutation requests. A stale
request returns `REVISION_CONFLICT` with the current revision.

The agent increments the revision once per accepted workspace mutation. PTY
output, terminal status changes, attach/detach, and resize do not increment
it. On conflict, the desktop fetches a snapshot and asks the user to repeat
the operation when needed.

Acceptance:

- Every snapshot exposes its revision.
- Two stale clients cannot silently overwrite workspace state.
- The desktop refreshes after a conflict.
- Terminal output does not change the workspace revision.

### 3. Remove socket I/O from the workspace lock

Refactor agent request handling so the state mutex covers only validation,
model mutation, and construction of response/event data:

```text
lock → validate/mutate/build data → unlock → write/enqueue data
```

Use a bounded outbound queue per connection. Start with the existing
connection model; do not introduce a general actor or event-bus framework.

Initial limits:

- 1,000 queued messages per connection
- 4 MiB queued bytes per connection

Disconnect a client that exceeds either limit. Preserve event order for each
client and never perform a socket read or write while the workspace mutex is
held.

Acceptance:

- A client that stops reading cannot freeze the agent.
- Queue memory is bounded.
- Per-client event order remains deterministic.
- The agent can continue serving other clients.

### 4. Make terminal creation transactional

Give terminal creation one local lifecycle record containing the child PID,
PTY, model object, registration state, page binding, and reader state. Publish
the terminal only after every creation step succeeds.

On failure, clean up in reverse order: stop the reader, remove page binding,
remove the model object, close the PTY, terminate the child, and wait for it.
Add failure-injection coverage around each acquisition step.

Acceptance:

- No leaked child or zombie remains after a failed create.
- No PTY or partial terminal remains in the model.
- Terminal-created events are emitted only after complete success.

### 5. Add deadlines, cancellation, and immediate security fixes

Use explicit defaults:

```text
connect  2s    handshake 2s    request 5s    shutdown 2s
```

Thread `GCancellable` through the desktop control-client API. Keep IPC off
the GTK main thread and distinguish timeout, protocol, and connection errors.

Also complete the existing security/build cleanup:

- remove the default Codex unsafe-mode/sandbox bypass;
- fail agent startup if socket permissions cannot be secured;
- use `$XDG_RUNTIME_DIR` for the default socket;
- validate socket ownership before using an existing endpoint;
- repair protobuf tool installation in CI;
- install `python3-xlib` for UI tests;
- run sanitizers against the agent, core, and control libraries as well as
  the desktop.

### Milestone 1 gate

The desktop can restart without disturbing surviving terminals, stale state
cannot overwrite newer agent state, a blocked client cannot freeze the agent,
terminal creation rolls back completely, socket setup fails closed, and all
requests have bounded waits. Existing unit, control, GTK, and stress tests
must pass.

## Milestone 2: prepare the existing protocol for more clients

### 6. Organize the single Protobuf contract

Keep `proto/sakura/control.proto` as the canonical schema. Group its contents
in-place into common types, workspace messages, terminal messages, requests,
responses, and events. Keep service/transport framing concerns separate from
domain messages through naming and comments rather than introducing many
files.

Rules:

- no GTK or widget concepts in messages;
- stable IDs instead of pointers or array positions;
- never reuse deleted field numbers;
- preserve compatibility when adding optional fields.

### 7. Stabilize errors and capabilities

Replace ad-hoc error strings where callers need to branch with a small stable
error set: invalid argument, not found, already exists, revision conflict,
invalid state, unsupported, unauthorized, timeout, and internal error.

Include a message, current revision when relevant, and a retryable flag. Extend
the existing handshake with protocol major/minor versions and capability bits.
Use the major version for breaking changes and capabilities for optional
features.

### 8. Add resumable terminal output

Extend terminal output with absolute start/end offsets and retain a bounded
buffer, initially about 1 MiB per terminal. A client can request output after a
known offset. If the offset is no longer retained, report an output gap and
let the client refresh its terminal view.

Do not make the buffer durable and do not turn terminal output into workspace
events.

### 9. Put the desktop behind a backend boundary

Introduce one small backend interface at the existing desktop/agent boundary.
The initial implementation remains the local Unix-socket client. The UI uses
the backend for snapshots, mutations, subscriptions, and terminal streams;
it does not inspect socket framing or transport-specific generated details.

Keep this interface in one focused header/implementation pair if needed. Do
not create a directory of one-file abstractions.

### Milestone 2 gate

The single Protobuf file is a stable domain contract, local errors and
capabilities are versioned, terminal output can resume or report a gap, and
the desktop UI no longer depends directly on local transport details. No
network listener exists in the agent.

## Milestone 3: add the minimal remote gateway

Start only after Milestones 1 and 2 pass their gates.

### 10. Add one gateway executable

Create `sakura-gateway` as a separate executable/package. It is responsible
for HTTPS/Connect, WebSocket streams, authentication, permissions, and a thin
adapter to the agent's existing local Unix-socket protocol.

Initial limits:

- disabled unless the user explicitly enables it;
- loopback by default;
- one local user and one workspace;
- short-lived pairing token or QR code;
- no cloud relay or multi-user account system.

The agent remains network-blind and unchanged as a network server.

### 11. Expose unary control and one binary stream

Begin with unary Connect methods for snapshot, workspace commands,
diagnostics, terminal create, resize, and close. Do not add Connect streaming
yet.

Use one binary WebSocket per remote client. Frames are Protobuf messages with
one sequence number and a terminal ID where applicable. The stream carries
subscription, workspace event, terminal input/output, resize, exit, and error
frames. Do not add WebTransport or a custom multiplexing framework.

### 12. Add pairing and revocation

The desktop explicitly enables remote access and displays a short-lived
pairing token. A paired device receives a device credential that can be
revoked from the desktop. Start with only:

- read-only access;
- terminal-control access.

Enforce permissions in the gateway for every remote request. The local agent
should still receive only authenticated, locally-originated gateway traffic.

### Milestone 3 gate

Remote access is opt-in, the gateway is the only network boundary, generated
Protobuf types are used by remote APIs, terminal input/output works over the
binary WebSocket, and revoked devices can no longer access the workspace.

## Milestone 4: validate the protocol with a small web client

Build only the useful slice:

- pair and show connection status;
- load the authoritative workspace snapshot;
- show groups, tasks, and terminals;
- perform basic rename/move operations;
- attach to an existing terminal;
- send input, receive output, resize, and reconnect from an offset.

On reload or WebSocket loss:

```text
fetch snapshot → reopen stream → request output after last offset
```

Handle expired credentials, revision conflicts, output gaps, exited
terminals, and multiple tabs. Do not reproduce the entire GTK layout or add
offline mutation queues.

### Milestone 4 gate

A browser can pair, view the current workspace, attach to a terminal, and
recover safely after reload or a dropped connection. The browser never becomes
a workspace authority.

## Milestone 5: add Android after web validation

Generate Kotlin types from the same single Protobuf schema and reuse the same
Connect API and WebSocket protocol. Start with snapshot loading, groups/tasks,
running terminals, terminal attachment, and basic create/close actions.

Use secure device-credential storage and a cached snapshot. On foreground:

```text
show cached snapshot → fetch authoritative snapshot → reconnect selected stream
```

Do not keep all terminal streams open in the background, and do not add
Android-specific state to the agent model.

## Testing and verification

Extend the current test layers rather than adding a new test framework:

- core tests for revisions, snapshot application, and invariants;
- control tests for handshake, errors, compatibility, and offsets;
- agent integration tests for reconnect, blocked clients, cleanup, and
  cancellation;
- GTK tests for projection/reconnect behavior;
- existing session stress tests for restart and process survival;
- sanitizer and clean CI builds for every local milestone.

Remote milestones add focused gateway/WebSocket tests and one browser smoke
test. They do not require a large end-to-end test harness initially.

## Recommended execution order

1. Land this plan and record the ownership rules.
2. Fix reconnect snapshot application.
3. Add workspace revisions and conflict refresh.
4. Move agent writes out of the workspace lock and bound queues.
5. Make terminal creation rollback-safe.
6. Add deadlines, cancellation, socket security, Codex, and CI fixes.
7. Stabilize the single Protobuf file, errors, capabilities, and offsets.
8. Add the small desktop backend boundary.
9. Only then build the gateway, pairing, and first web client.
10. Start Android only after the web protocol has survived real use.

## Explicitly deferred

Do not implement these as part of this plan:

- Connect transport in the C desktop or agent;
- many fine-grained `.proto` files or source directories;
- Connect streaming;
- WebTransport;
- durable event logs or terminal-output storage;
- cloud relay infrastructure;
- multi-user hosting;
- CRDT conflict merging;
- remote file synchronization;
- direct browser-to-agent connections;
- plugin protocol work.
