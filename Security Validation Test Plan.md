1. Document Overview

1.1 Purpose

This Security Validation Test Plan (SVTP) defines how to verify and validate the cybersecurity controls of the TRDP Simulator, based on the previously defined:

STRIDE Threat Model

Security Requirements (SR-01 … SR-10)

Overall system architecture (web backend + TRDP engine + XML config)


Security validation focuses on:

Authentication & authorization

Input validation

TRDP packet hardening

XML hardening

DoS resilience

Audit & logging

Memory safety



---

2. References

TRDP Simulator SRS (Security Requirements section)

TRDP Simulator SAS (Architecture, including security architecture)

TRDP Simulator STRIDE Threat Model v1.0

TRDP Simulator DDS & backend API specification

TRDP protocol spec (TCNopen) – for field semantics



---

3. Scope

Included in this SVTP:

Web API security (auth, roles, input validation)

XML configuration security (size, validation, permissions)

TRDP engine robustness to malformed PD/MD traffic

DoS resilience at HTTP level and TRDP packet level

Audit logging of security-relevant operations


Not in this SVTP (at least in initial version):

Underlying OS hardening (kernel-level security)

Full cryptographic protocol evaluation (TLS config)

Long-term cryptographic key management



---

4. STRIDE → Requirements → Test Mapping

4.1 Threats & Requirements Recap (short)

From the threat model, key Security Requirements (SR):

SR-01 – Authentication required

SR-02 – Role-based access control (RBAC)

SR-03 – Input validation for dataset writes

SR-04 – XML hardening (size, depth, schema)

SR-05 – TRDP packet hardening (drop malformed)

SR-06 – DoS mitigation (rate limiting, resource limits)

SR-07 – Audit logging of critical actions

SR-08 – Secure password storage

SR-09 – No shell command execution from untrusted input

SR-10 – Memory safety in TRDP adapter (bounds checks, sanitizers in debug)



---

4.2 STRIDE → Requirement → Test Case Matrix

Spoofing (S)

Threat ID	Description	Requirement(s)	Test Case IDs

S1	Spoofing web login / admin identity	SR-01, SR-02, SR-08, SR-07	SEC-001, SEC-002, SEC-003
S2	Spoofing TRDP device identity	SR-05, SR-07	NET-001, NET-003
S3	Spoofing XML input	SR-04, SR-07	XML-001, XML-002


Tampering (T)

Threat ID	Description	Requirement(s)	Test Case IDs

T1	Tampering JSON dataset values via API	SR-02, SR-03, SR-07	DATA-001, DATA-002, DATA-003
T2	TRDP packet tampering (malformed packets)	SR-05, SR-10, SR-07	NET-002, NET-004, FUZ-001
T3	XML configuration tampering	SR-04, SR-07	XML-001, XML-003


Repudiation (R)

Threat ID	Description	Requirement(s)	Test Case IDs

R1	Denying dataset/config changes	SR-07	LOG-001, LOG-002
R2	Denying XML reload / parameter change	SR-07	LOG-001, LOG-003


Information Disclosure (I)

Threat ID	Description	Requirement(s)	Test Case IDs

I1	Unauth dataset/config viewing	SR-01, SR-02	SEC-001, SEC-004
I2	XML topology/config disclosure	SR-04, SR-02	XML-004, SEC-004
I3	Logs leaking sensitive info	SR-07	LOG-004


Denial of Service (D)

Threat ID	Description	Requirement(s)	Test Case IDs

D1	HTTP flood DoS	SR-06	DOS-HTTP-001
D2	PD packet flood	SR-06, SR-05	DOS-PD-001
D3	MD session storm	SR-06, SR-05	DOS-MD-001
D4	Malicious XML causing parse DoS	SR-04, SR-06	XML-005


Elevation of Privilege (E)

Threat ID	Description	Requirement(s)	Test Case IDs

E1	Viewer → Admin escalation via API	SR-02, SR-01, SR-07	SEC-002, SEC-003, LOG-002
E2	Memory corruption in TRDP adapter	SR-05, SR-10	FUZ-001, FUZ-002
E3	XML injection leading to command execution	SR-09, SR-04, SR-07	XML-006



---

5. Security Test Cases (Detailed)

Below is a catalog of security-focused test cases grouped by category. You can expand each with more granularity later.


---

5.1 Authentication & Authorization Tests

TC: SEC-001 – Enforce Authentication for Protected APIs

Linked Threats: S1, I1
Requirements: SR-01

Objective: Verify that all protected endpoints require authentication.

Scope: /api/pd, /api/datasets, /api/md/*, /api/config/*, /api/diag/*, /api/status.


Preconditions:

Simulator deployed with authentication enabled.

HTTP client (curl/Postman) available.


Steps:

1. Without any authentication (no cookie/token), call:

GET /api/pd

GET /api/datasets

POST /api/datasets/1001/elements/0 (test write op)



2. Observe HTTP response codes and bodies.



Expected Results:

All such requests return 401 Unauthorized or 403 Forbidden (based on your auth design).

No dataset values or PD configuration details are leaked in the body.

Diagnostic log contains entries for unauthorized access attempts.



---

TC: SEC-002 – Role Separation: Viewer vs Dev vs Admin

Linked Threats: S1, E1, I1, T1
Requirements: SR-02

Objective: Verify that roles have correct permissions:

Viewer: read-only, no config changes.

Dev: can modify datasets but not config.

Admin: full control.


Preconditions:

User accounts exist: viewer, dev, admin.

Roles correctly configured.


Steps:

1. Login as viewer, obtain session/token.

Try GET /api/datasets (should succeed).

Try POST /api/datasets/1001/elements/0 (should fail).

Try POST /api/config/reload (should fail).



2. Login as dev.

GET /api/datasets (OK).

POST /api/datasets/1001/elements/0 (OK).

POST /api/config/reload (should fail).



3. Login as admin.

All above operations should succeed.




Expected Results:

Access control exactly matches role matrix.

Failures return clear error JSON { "ok": false, "error": "insufficient_permissions" }.

Audit log records who attempted what and whether it was allowed.



---

TC: SEC-003 – Password Storage & Login Bruteforce Protection

Linked Threats: S1
Requirements: SR-08

Objective: Verify passwords are not stored/returned, and bruteforce is limited.

Preconditions:

Access to DB / credential store (in test environment).


Steps:

1. Inspect user store: ensure no plaintext passwords; only hashes.


2. Attempt multiple failed logins for the same account (e.g. 10 incorrect passwords).


3. Inspect logs for suspicious login attempts.



Expected Results:

Password field is hashed (e.g. bcrypt/argon2 representation).

No plaintext passwords present.

Login attempts beyond threshold trigger temporary lockout or throttling.

Audit log contains failed login entries.



---

TC: SEC-004 – Prevent Unauthorized Data / Config Disclosure

Linked Threats: I1, I2
Requirements: SR-01, SR-02

Objective: Confirm that unauthorized users cannot read sensitive configuration/topology or dataset values.

Steps:

1. Without auth, attempt GET /api/config/summary, GET /api/datasets.


2. With Viewer role, attempt GET /api/config/summary.


3. With Viewer role, attempt GET /api/datasets (read-only is fine).



Expected:

Unauth: blocked.

Viewer: can read dataset values but not detailed config if you decide to restrict it (design choice).

Admin: full visibility.



---

5.2 Input Validation & Dataset Protection

TC: DATA-001 – Type & Range Validation for Dataset Writes

Linked Threats: T1, E2
Requirements: SR-03

Objective: Verify backend enforces type/limits for dataset elements.

Preconditions:

Dataset with UINT8, UINT16, CHAR8, arrays, etc.


Steps:

1. Attempt to write -1 to a UINT8 field via /api/datasets/{id}/elements/{idx}.


2. Attempt to write 1000 to a UINT8 field.


3. Attempt to write a string to an integer field.


4. Attempt to write an array longer than arraySize.



Expected:

All invalid writes rejected with structured error:
{"ok": false, "error": "invalid_value", "details": "..."}

Dataset instance remains unchanged.

No crash, no memory corruption.



---

TC: DATA-002 – Locking Prevents Writes

Linked Threats: T1
Requirements: SR-03

Objective: Confirm locked flag is honored.

Steps:

1. Lock dataset via POST /api/datasets/{id}/lock { "locked": true }.


2. Attempt to write any element.


3. Read dataset back.



Expected:

Write fails; dataset values unchanged.

API returns clear error (e.g. "dataset_locked").



---

TC: DATA-003 – Clear & Clear-All Input Handling

Linked Threats: T1
Requirements: SR-03

Objective: Verify clearing operations behave correctly, no invalid states.

Steps:

1. Set all elements to valid values.


2. Call POST /api/datasets/{id}/elements/{idx}/clear.


3. Call POST /api/datasets/{id}/clear-all.


4. Fetch dataset.



Expected:

Cleared elements have defined=false, value=null.

No leftover raw bytes.



---

5.3 XML Hardening

TC: XML-001 – XML Schema Validation (Positive)

Linked Threats: T3, S3
Requirements: SR-04

Objective: Ensure valid XML passes and is applied.

Steps:

1. Use your known-good XML (like the ones you shared).


2. Reload config (POST /api/config/reload).


3. Check /api/config/summary.



Expected:

Reload succeeds.

Summary matches XML content.



---

TC: XML-002 – XML Schema Validation (Negative – Missing Attributes)

Linked Threats: T3, S3
Requirements: SR-04

Objective: Ensure required attributes are enforced.

Steps:

1. Remove required attribute (com-id, data-set-id, or bus-interface name).


2. Attempt reload.



Expected:

Reload fails; previous config remains active.

API returns {"ok": false, "error": "xml_validation_failed"}.

Diagnostic log records details.



---

TC: XML-003 – Duplicate IDs and Out-of-Range Values

Linked Threats: T3
Requirements: SR-04

Steps:

1. Create XML with duplicate data-set id or com-id.


2. Create XML with negative cycle-time or absurd values (e.g. cycle-time="0" or ttl="999").



Expected:

Reload rejected with clear error.



---

TC: XML-004 – Config Confidentiality in UI

Linked Threats: I2
Requirements: SR-04, SR-02

Objective: Ensure that only authorized roles can see full topology.

Steps:

1. As Viewer, call /api/config/summary.


2. Inspect if sensitive fields (raw IPs, low-level parameters) are either hidden or allowed based on your policy.



Expected:

Behavior matches defined role policy.



---

TC: XML-005 – XML Parsing DoS (Size/Depth)

Linked Threats: D4
Requirements: SR-04, SR-06

Steps:

1. Create extremely large XML (e.g., >10MB) or deeply nested structures.


2. Try /api/config/reload.


3. Monitor CPU and memory.



Expected:

Reload is rejected early with an error (size/depth exceeded).

No long hangs or huge memory spikes.



---

TC: XML-006 – No Command Execution from XML

Linked Threats: E3
Requirements: SR-09

Steps:

1. Put suspicious strings in XML fields like host-name="; rm -rf /" or uri="| /bin/sh".


2. Reload config.


3. Observe logs and OS behavior.



Expected:

Simulator treats values as plain data.

No external command execution, no side effects on system.



---

5.4 TRDP Network Robustness & Fuzzing

TC: NET-001 – TRDP Source Spoofing Detection (Basic)

Linked Threats: S2
Requirements: SR-05, SR-07

Objective: Check that simulator logs source IP/port and optionally supports basic whitelisting.

Steps:

1. Configure simulator to subscribe to certain PD comId.


2. Send valid TRDP PD packets from two different IPs:

expected sender

unexpected (spoof) sender



3. Observe logs and UI.



Expected:

Both sources visible with IP in diagnostics.

If whitelisting is configured, spoofed source is dropped or flagged.



---

TC: NET-002 – Malformed PD Packet (Length Mismatch)

Linked Threats: T2, E2
Requirements: SR-05, SR-10

Steps:

1. Use Scapy or custom tool to send PD packets with data-set length shorter/longer than expected.


2. Monitor simulator behavior.



Expected:

Packet is dropped; dataset not updated.

No crash, no memory violation.

Log entry like "pd_malformed_packet_dropped".



---

TC: NET-004 – Malformed MD Packet (Invalid Session ID)

Linked Threats: T2, E2
Requirements: SR-05, SR-10

Steps:

1. Send MD reply with unknown sessionId.


2. Send MD with incorrect length vs dataset.



Expected:

Unknown sessions are ignored.

MD engine remains stable.



---

TC: FUZ-001 – PD Fuzzing with Sanitizers (Debug Build)

Linked Threats: E2
Requirements: SR-10

Objective: Detect memory safety issues.

Steps:

1. Build simulator with -fsanitize=address,undefined.


2. Use fuzzing tool (e.g., AFL, libFuzzer, or custom script) to generate random PD payloads.


3. Inject into TRDP RX path (or directly call unmarshaling with fuzz data).



Expected:

No sanitizer violations.

If violations occur, they are fixed before release.



---

TC: FUZ-002 – MD Fuzzing

Same approach, but for MD payloads.


---

5.5 DoS Resilience

TC: DOS-HTTP-001 – HTTP Request Flood

Linked Threats: D1
Requirements: SR-06

Steps:

1. Use ab, siege, wrk, or a custom script to send high rate requests to /api/datasets and /api/pd.


2. Ramp up to N requests/second (e.g. 500–1000).


3. Track simulator responsiveness and CPU/memory.



Expected:

Simulator stays responsive or gracefully rejects requests with 429 Too Many Requests (if implemented).

No crash; CPU may increase but stays within acceptable bounds.



---

TC: DOS-PD-001 – PD Flood

Linked Threats: D2
Requirements: SR-06, SR-05

Steps:

1. Use a TRDP or raw UDP generator to flood PD packets at high rate.


2. Monitor PD engine stats, CPU, and latency of normal operations.



Expected:

PD engine may drop surplus packets but continues to operate.

No memory leak or crash.



---

TC: DOS-MD-001 – MD Session Storm

Linked Threats: D3
Requirements: SR-06

Steps:

1. Programmatically create hundreds of MD sessions from another node.


2. Send many requests simultaneously to simulator.



Expected:

Simulator enforces maximum active sessions.

Requests beyond limit are rejected or queued in a controlled fashion.



---

5.6 Logging & Audit

TC: LOG-001 – Audit Log for Security-Relevant Events

Linked Threats: R1, R2
Requirements: SR-07

Steps:

1. Perform:

Successful login (for each role)

Failed login

Dataset write

Dataset lock/unlock

Config reload



2. Inspect audit log.



Expected:

Each event appears with: timestamp, user, action, target, result (success/failure).



---

TC: LOG-002 – Tamper Detection via Logs

Linked Threats: R1
Requirements: SR-07

Objective: Show that even if action is denied, it is logged.

Steps:

1. Viewer tries to call /api/config/reload.


2. Inspect logs.



Expected:

Log contains denied operation with viewer identity.



---

TC: LOG-004 – Sensitive Data Not Over-Logged

Linked Threats: I3
Requirements: SR-07

Steps:

1. Execute operations with sensitive values in datasets.


2. Check logs do not dump full dataset contents unless necessary and permitted.



Expected:

Logs contain summary, not full raw data (depending on your policy).



---

6. Test Execution & Reporting

For each test case:

Record:

Test ID / Name

Date / Tester

Version of simulator

Result (Pass / Fail / Blocked)

Linked defect ID (if any)



Summarize in Security Test Report (STR), highlighting:

All STRIDE categories have coverage.

All High priority threats have at least one passing test.
