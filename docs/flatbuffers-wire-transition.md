# Transition Guide: JSON Envelope to Full FlatBuffers Wire Protocol

This guide documents how Jubilant DB will move from the v0.0.2 JSON wire prototype to the long-term FlatBuffers-based wire protocol. It captures why the change matters, the rollout phases, required code changes, expected roadblocks, and validation steps so that future contributors can land the migration without guesswork.

## Goals and success criteria

- Adopt FlatBuffers for all request/response payloads on the wire, matching the storage- and WAL-level FlatBuffers usage already planned in the specifications.
- Preserve operability during the transition by shipping a dual-stack path (JSON + FlatBuffers) until all clients migrate.
- Maintain frame-level safety: length-prefixed framing, maximum frame limits, file identifiers, and size-prefixed FlatBuffers roots with verification on receive.
- Keep schema evolution explicit: file identifiers, versioning fields, and compatibility rules must be enforced before cutting over.

The migration is complete when the default server listener speaks FlatBuffers, `jubectl`/SDKs default to FlatBuffers, JSON is removed (or guarded by a legacy flag), and integration tests cover FlatBuffers-only flows end to end.

## Current state

- The v0.0.2 prototype speaks length-prefixed JSON for `set/get/del/txn` as documented in `docs/txn-wire-v0.0.2.md`.
- Server runtime, worker pool, and transaction planner operate on native `txn::TransactionRequest/Result` structs and do not depend on the payload format.
- FlatBuffers is already the designated binary format for disk/WAL (see `MAIN_SPECIFICATION.md` and `TECH_SPECIFICATION.md`), but the wire path has not yet switched.

## Why FlatBuffers on the wire

- **Performance and GC behavior:** zero-copy reads after verifier checks, reduced allocations compared to JSON parsing.
- **Type safety + schema lock-in:** enums, unions, and identifier enforcement prevent schema drift; file identifiers guard against payload mix-ups.
- **Binary value fidelity:** byte arrays and mixed value kinds (string/int/bytes) remain unambiguous without base64/hex wrappers.
- **Shared codegen:** reuse `schemas/wire.fbs` across C++ server, C++/Python/other SDKs, and tests; aligns with WAL/disk generators.
- **Forward/backward compatibility:** size-prefix + version fields allow receivers to reject or downgrade gracefully; schema evolution can follow the same rules as disk formats.

## Rollout phases

1. **Schema finalization + codegen plumbing**
   - Finalize `schemas/wire.fbs` with `TxnRequest`, `TxnResponse`, op enums, error codes, and per-field defaults. Add file identifiers and a protocol version field.
   - Extend CMake presets (`dev-debug`, `dev-debug-tidy`) to generate wire headers and expose them to both server and client libraries. Keep generator version pinned (see `TECH_SPECIFICATION.md`).
   - Add round-trip unit tests that build FlatBuffers payloads and verify decode into existing transaction structs.

2. **Dual-stack server listener**
   - Introduce a FlatBuffers-aware framing layer (`src/wire/framing.*` planned in the roadmap) that reads a size-prefixed buffer, verifies via `flatbuffers::Verifier`, and dispatches by file identifier.
   - Keep the current JSON path available behind a config flag (`wire_mode = json|flatbuffers|auto`) to avoid breaking existing tests and clients.
   - Add feature flags or environment toggles to enable FlatBuffers per deployment while collecting metrics.

3. **Client/SDK parity**
   - Update `jubectl` and any SDKs to emit FlatBuffers by default while retaining JSON fallback behind a CLI flag (e.g., `--wire=json`).
   - Provide helper builders for common ops so client code does not hand-roll FlatBuffers offsets.
   - Add integration tests that exercise both wire modes against the same server build.

4. **Cutover and removal**
   - Flip the server default to FlatBuffers and mark JSON as deprecated in docs and help text.
   - Remove JSON-only code paths once clients are migrated, leaving a minimal compatibility shim if necessary for downgrade testing.
   - Update docs and quickstarts to show only FlatBuffers examples; archive JSON examples alongside a deprecation note.

## Implementation map (by area)

- **Schemas (`schemas/wire.fbs`):**
  - Add `file_identifier` and `protocol_version` fields; ensure size-prefixed roots for every message.
  - Define enums for op types and error codes; use unions for result payloads where value kinds differ.
  - Include reserved/extension fields to allow future additions without breaking older readers.

- **Server wire layer (`src/wire/`):**
  - Build `framing.{h,cc}` to read `uint32` length prefixes (network byte order), enforce max frame size, and verify FlatBuffers roots.
  - Add `handlers.{h,cc}` to translate decoded FlatBuffers into `txn::TransactionRequest` and serialize `TransactionResult` back to FlatBuffers.
  - Provide a `WireMode` enum and configuration hook to select JSON vs. FlatBuffers at runtime; default to FlatBuffers once stable.

- **Clients and tooling (`tools/jubectl/`, SDKs):**
  - Generate client-side FlatBuffers headers during the build; ship pre-generated artifacts for scripting languages if codegen is not available at runtime.
  - Expose high-level helpers (e.g., `BuildSetRequest`, `ParseTxnResponse`) to avoid manual buffer assembly.
  - Keep framing helpers shared between JSON and FlatBuffers to minimize duplication.

- **Tests (`tests/`):**
  - Unit: verify FlatBuffers verifier failures on truncated/oversized frames; round-trip value kinds and error paths.
  - Integration: start the server in FlatBuffers mode, issue transactions via client helpers, and assert responses and on-disk state.
  - Fuzz/property: feed random-but-valid FlatBuffers payloads through the verifier and decoder to harden boundaries.

## Roadblocks and mitigations

- **Schema churn risk:** Lock schema ownership before rolling codegen; publish versioned schema snapshots and enforce file identifiers so stale clients fail fast.
- **Generator drift:** Pin FlatBuffers version in CMake (`FetchContent_MakeAvailable`) and mirror it in client build instructions; add a presubmit check that regenerated headers match the repository.
- **Mixed deployments:** During dual-stack mode, ensure clear negotiation rules (config-driven rather than auto-detect) to avoid ambiguity when payloads fail verification.
- **Large payloads:** Preserve the existing frame size cap; verify buffers before allocation to avoid unbounded memory growth.
- **Error reporting:** Map verifier failures to structured error responses so clients can distinguish framing errors from application errors.
- **Testing debt:** Without FlatBuffers integration tests, regressions will hide; make FlatBuffers mode mandatory in CI once dual-stack is stable.

## Validation checklist (per PR)

- [ ] `schemas/wire.fbs` updated with identifiers/versioning and regenerated headers committed.
- [ ] Server framing verifies size-prefixed FlatBuffers roots and rejects unknown identifiers.
- [ ] `jubectl`/SDK default to FlatBuffers with a documented JSON escape hatch.
- [ ] Integration tests cover FlatBuffers requests/responses and storage side-effects.
- [ ] Documentation and examples reference FlatBuffers payloads; JSON examples marked legacy.

## Advantages after cutover

- Simplified payload handling (no JSON translation layers) and lower latency under load.
- Unified format across wire, WAL, and disk makes debugging and tooling consistent.
- Safer evolution story via schema identifiers, explicit versioning, and verifier-backed boundaries.
- Better client ergonomics: generated types, enums, and unions remove ambiguity around values and error codes.

## Next steps

1. Finalize `schemas/wire.fbs` and land CMake codegen wiring.
2. Implement FlatBuffers framing + handlers under `src/wire/` with configuration toggles.
3. Update `jubectl`/SDKs to default to FlatBuffers and add integration tests covering both modes.
4. Switch CI and documentation to FlatBuffers-only, then remove JSON paths once downstreams are migrated.

Following this guide should keep the migration predictable, verifiable, and reversible while delivering the long-term FlatBuffers benefits.
