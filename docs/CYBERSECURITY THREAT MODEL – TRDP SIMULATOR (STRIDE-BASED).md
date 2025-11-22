🔒 CYBERSECURITY THREAT MODEL – TRDP SIMULATOR (STRIDE-BASED)

Version 1.0 — Suitable for EN 50701 / IEC 62443 style cybersecurity engineering


---

1. Introduction

The TRDP Simulator is a Linux-based application designed to act as a TRDP end device (PD/MD publisher, subscriber, requester, responder). It includes:

C++ backend

Web server (Drogon)

Web UI (React or similar)

XML-based configuration

Real-time PD/MD I/O

Diagnostics, logging, event monitoring


Because TRDP is widely used in train control and communication networks, the simulator interacts with safety-adjacent traffic, making a formal threat model essential.

This document follows the STRIDE model:

Category	Description

S	Spoofing identity
T	Tampering with data
R	Repudiation
I	Information disclosure
D	Denial of Service
E	Elevation of privilege



---

2. System Assets

Assets considered critical for confidentiality, integrity, and availability:

2.1 Operational Assets

TRDP PD/MD traffic

PD cyclic timing & determinism

MD session states (timeouts, replies, retries)

Dataset values (may encode control/diagnostic parameters)

TRDP configuration (XML)

Network interface configurations

Web API states

Event & diagnostic logs


2.2 Security Assets

Authentication credentials

Session tokens / authorization context

User roles (Admin/Dev/Viewer)

Logs with audit trail

Config files (XML defining comId, interface IPs, dataset types)



---

3. System Surfaces & Trust Boundaries

STRIDE threats are evaluated at each boundary.

TB1 — User Browser ↔ Web Backend (HTTP API)

Public API accessible over network. Vulnerable to typical web threats.

TB2 — Web Backend ↔ Simulator Internal Engine

Internal API, but can be abused indirectly through malformed input.

TB3 — Simulator ↔ TRDP Network

Untrusted multicast/broadcast domain. Anyone on network can send TRDP.

TB4 — Simulator ↔ File System (XML config, logs)

Threat of external file tampering, injection, malicious XML.

TB5 — Simulator Runtime ↔ OS Kernel

Resource limits, DoS, privilege escalation.


---

4. STRIDE Threats by Trust Boundary (Deep Version)


---

4.1 Spoofing Threats (S)

S1: Spoofing Web Login (TB1)

Threat: Attacker pretends to be Admin/Dev.
Impact: Ability to modify datasets, issue MD requests, change XML config.
Mitigations:

Strong password hashing (bcrypt/argon2).

Session tokens signed using HMAC/secret key.

HTTPS for production deployment.

Account lockout / rate limiting.

Disable default passwords on first use.



---

S2: Spoofing TRDP Device Identity (TB3)

Threat: Malicious TRDP device sends packets pretending to be a trusted train component.
Impact: Wrong dataset values, false status, misdiagnosis.
Mitigations:

Record sender IP, port, NIC.

Allow optional whitelisting per interface.

Warn user on unexpected multicast source.



---

S3: Spoofing XML Input (TB4)

Threat: User supplies XML that masquerades as a valid TRDP device definition.
Impact: Misconfigures simulator.
Mitigations:

XML schema validation (XSD).

Enforce integer ranges for IDs/cycle times.

Reject unknown tags.



---

4.2 Tampering Threats (T)

T1: JSON Tampering with Dataset Values (TB1)

Threat: User alters PD output dataset to dangerous values.
Impact: Incorrect test data pushes wrong info to real control systems.
Mitigations:

Role enforcement: Viewer cannot write.

Type & range validation.

Lock mode for outgoing datasets.

Limit max update frequency per comId.



---

T2: TRDP Packet Tampering (TB3)

Threat: Malformed or specially-crafted PD/MD packets injected.
Impact: Memory corruption, crashes, undefined backend behavior.
Mitigations:

Strict bounds checking when unmarshaling.

Discard packets with unexpected sizes or dataset mismatch.

Do not trust TRDP header fields blindly.



---

T3: XML Configuration Tampering (TB4)

Threat: XML edited externally to alter network IDs, IPs, comIds.
Mitigations:

File permission hardening (chmod 600).

Check that XML “owner” user is trusted.

Digital signature for XML configs (optional advanced feature).



---

4.3 Repudiation Threats (R)

R1: User Denies Changing Dataset (TB1)

Threat: User claims they never issued a PD write.
Mitigations:

Detailed audit logging (user ID, IP, timestamp, dataset ID).

Versioning of changes.



---

R2: Denying XML Reload or Parameter Change (TB1/TB4)

Threat: Admin denies having reloaded config.
Mitigation:

Log all config reloads + file checksum.

Keep last 5 versions in backup.



---

4.4 Information Disclosure Threats (I)

I1: Unauthorized Viewing of Datasets (TB1)

Threat: Unauthenticated user reads PD/MD values (which may be confidential).
Mitigations:

Require authentication.

Viewer vs Dev vs Admin permissions.



---

I2: Disclosure of Network Topology & Timing (XML) (TB4)

Threat: XML reveals host IPs, TRDP comIds, message wiring.
Mitigations:

Restrict access to XML file.

Remove sensitive fields in UI unless authorized.

Optionally encrypt XML at rest.



---

I3: Leakage of Logs Containing Sensitive Info (TB4)

Threat: Logs may contain device IPs, dataset values.
Mitigations:

Protect log directories.

Clean PIIs or sensitive values from logs.

Log rotation.



---

4.5 Denial of Service Threats (D)

D1: HTTP Flood (TB1)

Threat: Large volume of GET/POST requests overwhelm server.
Mitigations:

Rate limit per IP.

Drogon thread pool tuning.

Reject body sizes above limits.



---

D2: PD Flood from TRDP Network (TB3)

Threat: High multicast flooding causes CPU overload.
Mitigations:

Per-interface packet processing quotas.

Drop low-priority packets first.

Show warning in diagnostics.



---

D3: MD Storm Attack (TB3)

Threat: Excessive MD requests causing queue exhaustion.
Mitigations:

Limit MD session count per peer IP.

Timeout cleanup of stale sessions.



---

D4: Malicious XML causing Parser DoS (TB4)

Threat: Very large XML with deep nesting, causing long parse times.
Mitigations:

Max XML file size (e.g., <5 MB).

Limit recursion depth.

Timeout XML loading.



---

4.6 Elevation of Privilege Threats (E)

E1: Viewer Becoming Admin (TB1)

Threat: Low privilege user accesses admin-only endpoint.
Mitigations:

Role-based middleware in backend.

JWT roles encoded & verified.

Server-side verification (never trust client role).



---

E2: Exploitation via Memory Corruption in TRDP Adapter (TB3)

Threat: Crafted TRDP packet overflow leads to arbitrary code execution.
Mitigations:

Use sanitizers (ASan, UBSan) in debug builds.

Strict index bounds.

Use safe C++ (no raw buffers without checks).

Avoid dangerous C functions in TRDP interfacing code.



---

E3: Privilege Escalation via XML Injection (TB4)

Threat: XML contains paths or shell-like data that backend mistakenly processes.
Mitigations:

Never call system() with XML data.

Treat XML strictly as config, not commands.

Validate all path/URI fields.



---

5. Threat Priority Matrix (DREAD)

A simple scoring:

Threat ID	Damage	Reproducibility	Exploitability	Affected Users	Discoverability	Total	Priority

D1 HTTP DoS	High	High	High	All	High	Very High	Critical
E2 Memory Corruption	High	Medium	Medium	All	Medium	High	High
S1 Spoof Login	High	High	Medium	All	High	High	High
I1 Unauthorized dataset read	Medium	High	High	Many	High	High	High
T2 TRDP packet tampering	Medium	Medium	Medium	All	Medium	Medium	Medium
R1 Repudiation	Low	High	Low	Few	High	Medium	Low



---

6. Security Requirements Derived from STRIDE

These become SRS / SAS security requirements.

SR-01 — Authentication Required

All non-static endpoints SHALL require authentication.

SR-02 — Role-Based Access Control

Admin-only operations SHALL be protected by server-side RBAC.

SR-03 — Input Validation

Backend SHALL validate all dataset writes (type, range, length).

SR-04 — XML Hardening

XML parsing SHALL enforce size, depth, schema.

SR-05 — TRDP Packet Hardening

Malformed PD/MD packets SHALL be dropped.

SR-06 — DoS Mitigation

Rate limit HTTP and packet processing.

SR-07 — Audit Logging

All configuration changes and dataset writes SHALL be logged with user identity.

SR-08 — Secure Storage

Passwords SHALL be hashed securely.

SR-09 — No Raw Command Execution

Backend SHALL not execute shell commands based on XML or API input.

SR-10 — Memory Safety (TRDP Adapter)

Use safe code patterns; apply sanitizers in debug builds.


---

7. Data Flow Diagram (DFD) — High-Level

+-------------------+
      |  Web Browser      |
      +-------------------+
                |
         TB1    | (HTTPS/HTTP API)
                v
      +-------------------+
      |   Web Backend     |
      | (Drogon)          |
      +-------------------+
        |         |
   TB2  |         | TB4
        v         v
+-------------------+      +-------------------+
|  Simulator Engine |      |  XML/Logs FS      |
| (PD/MD/DataSets)  |      |                   |
+-------------------+      +-------------------+
         |
         | TB3 (TRDP PD/MD)
         v
+---------------------------+
|  TRDP LAN (Untrusted)     |
+---------------------------+


---

8. Summary

This threat model highlights:

Critical threats → Spoofing login, DoS via HTTP/PD flood, Memory safety in TRDP unmarshalling, Unauthorized dataset access

Controls → RBAC, input validation, rate limiting, sanitizers, XML hardening

Logs, audit trails, and strict boundaries ensure traceability

It aligns well with EN 50701 and IEC 62443-4-1 secure development practices

