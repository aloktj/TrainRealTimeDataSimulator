# SRS Coverage Analysis

This document summarizes how the current codebase addresses the functional and non-functional requirements in `docs/Software Requirements Specification.md` and highlights notable gaps.

## Functional requirements

- **FR-1: XML configuration management – Implemented.** `ConfigManager` and `XmlConfigurationLoader` parse TRDP XML, validate schema, raise line-specific errors, and construct device, interface, dataset, and telegram structures used by the runtime.【F:src/config_manager.cpp†L14-L204】【F:src/xml_loader.cpp†L7-L24】
- **FR-2: TRDP PD communication – Largely implemented.** `PdEngine` builds publish/subscribe runtimes from XML, joins multicast groups, supports redundancy through publication channels, allows enable/disable and "send now" controls, and tracks RX/TX stats plus timeout state for jitter-aware status reporting.【F:src/pd_engine.cpp†L66-L155】【F:src/pd_engine.cpp†L200-L334】
- **FR-3: TRDP MD communication – Implemented.** `MdEngine` constructs requester/responder sessions from configuration, enforces session limits, supports TCP/UDP selection, and applies simulation rules (loss/delay/corruption) before dispatching via the TRDP adapter.【F:src/md_engine.cpp†L16-L120】【F:src/md_engine.cpp†L186-L330】
- **FR-4: Dataset handling and UI behaviors – Backend implemented, UI pending.** Dataset schemas are modeled with primitive and nested types, marshalled/unmarshalled for PD/MD flows, and exposed via JSON API endpoints, including status (Active/Inactive), lock/clear controls, and validation of element sizes. No industrial web UI exists; interaction is via the HTTP API only.【F:src/backend_api.cpp†L18-L115】【F:src/backend_api.cpp†L204-L332】【F:README.md†L51-L78】
- **FR-5: Industrial-grade web interface – Not implemented.** The system exposes a JSON HTTP API but does not ship a browser-based HMI, dashboards, or role-based page-level controls described in the SRS.【F:README.md†L51-L78】
- **FR-6: Multicast and NIC management – Implemented.** Multicast groups from XML are auto-joined, and the API allows manual join/leave with NIC overrides; interface binding is applied when initializing PD telegrams.【F:src/pd_engine.cpp†L66-L115】【F:README.md†L68-L77】
- **FR-7 & FR-11: Diagnostics, logging, exports – Partially implemented.** Diagnostic events and metrics are collected and served via `/api/diag/*`, and PCAP capture/rotation is configurable through XML or CLI flags. However, automated log/PCAP export workflows and an event browser UI are absent.【F:README.md†L68-L83】
- **FR-8: Advanced rail-industry testing (error injection, stress, redundancy) – Partially implemented.** Simulation controls allow PD/MD loss, delay, COM ID/dataset corruption, and sequence manipulation in both engines; redundancy channels are supported for PD publishers. Dedicated stress mode, time-sync display, and multi-device orchestration are not present.【F:src/pd_engine.cpp†L20-L49】【F:src/md_engine.cpp†L16-L45】【F:src/pd_engine.cpp†L92-L109】
- **FR-9: Scripting/automation – Implemented.** `scripts/scripting_hooks.py` drives PD/MD flows and captures pass/fail reports with exported diagnostics as documented in the scripting guide.【F:README.md†L85-L87】
- **FR-10: Security controls (RBAC, hardening) – Partially implemented.** `AuthManager` provides login, PBKDF2 hashing, session tokens, role parsing, and theme preference tracking, but there is no UI integration, rate limiting, or explicit EN 50701 hardening beyond password handling.【F:src/auth_manager.cpp†L15-L122】

## Non-functional requirements

- **Performance & reliability (NFR-1/NFR-2) – Partially addressed.** Engines run in dedicated threads with timeout/jitter tracking and support for redundant channels, but there are no measured performance targets or resilience tests recorded in the repository.【F:src/pd_engine.cpp†L200-L334】【F:src/md_engine.cpp†L224-L330】
- **Usability & UI (NFR-3/NFR-4) – Not met.** Without the industrial web interface, usability goals (dashboards, responsive design, theme switching) remain unimplemented; current access is via JSON API calls.【F:README.md†L51-L78】
- **Portability (NFR-5) – Addressed.** Build scripts support stub TRDP mode and external TRDP SDKs, with Docker, Debian packages, and Raspberry Pi build instructions documented for deployment.【F:README.md†L21-L41】【F:README.md†L102-L108】
- **Maintainability & security (NFR-6) – Partially addressed.** Modular components (engines, adapters, managers) and automated tests via CTest aid maintainability, but comprehensive security validation and regression test artifacts are not wired into automation.【F:README.md†L89-L101】【F:src/auth_manager.cpp†L15-L122】

## Overall status

The backend substantially covers XML parsing, PD/MD communication, dataset management, scripting hooks, multicast control, and diagnostics APIs. Major remaining gaps relative to the SRS are the industrial-grade web UI, richer security hardening, automated export/reporting flows, and performance/usability validation.
