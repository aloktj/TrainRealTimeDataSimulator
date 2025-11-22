1. Software Test Plan (STP)
1.1 Purpose
This Software Test Plan defines the strategy, scope, approach, resources, and schedule for testing the TRDP Simulator software (PD, MD, XML, Web UI, diagnostics, security).
1.2 Test Items
	•	TRDP Backend Engine
	◦	PD Engine (Publish/Subscribe)
	◦	MD Engine (Request/Response)
	◦	TRDP Stack Adapter (TCNOpen)
	◦	Dataset Manager
	◦	Interface Manager (NICs, multicast)
	•	Web Server Layer
	◦	REST API endpoints (/api/pd, /api/datasets, /api/md/..., /api/config/..., /api/diag/...)
	◦	WebSocket real-time channel (if implemented)
	•	Industrial Web UI Frontend
	◦	Dataset tables
	◦	PD/MD status dashboards
	◦	Config summary
	◦	Event log
	•	Configuration Manager
	◦	XML parsing & validation
	◦	Model generation
	•	Diagnostics & Logging
	◦	Event logger
	◦	PCAP export (if implemented)
	•	Security & Role Management
	◦	Authentication
	◦	Role-based access (Admin/Dev/Viewer)
1.3 Features to be Tested
	1	PD Communication
	◦	PD publish cycles, timing
	◦	PD subscription correctness
	◦	Unicast & multicast
	◦	Validity behavior (zero/keep)
	◦	Redundant PD behavior (if implemented)
	2	MD Communication
	◦	MD requester sessions
	◦	MD responder behavior
	◦	TCP vs UDP MD
	◦	Timeouts & retries
	3	Datasets
	◦	Parsing from XML
	◦	Mapping to runtime structures
	◦	Read-only (incoming) vs writable (outgoing) views
	◦	Array and nested dataset handling
	4	XML Configuration
	◦	Valid XML load
	◦	Invalid XML handling
	◦	Re-load at runtime
	5	Web API & UI
	◦	Endpoint correctness & schemas
	◦	Live status updates
	◦	Edit, locking, clearing datasets
	◦	PD enable/disable controls
	◦	MD session creation, send, view
	6	Diagnostics
	◦	Event logging
	◦	Error conditions visible in UI
	◦	Performance counters
	7	Performance & Stress
	◦	PD telegram counts (e.g. 100, 500, 1000)
	◦	MD sessions (e.g. 50, 200)
	◦	Cycle-time jitter
	8	Security
	◦	Authentication & sessions
	◦	Authorization by role
	◦	Input validation
	◦	Basic DoS resilience (rate limiting, timeouts)
1.4 Features Not to be Tested (Initial Phase)
	•	Full SIL2/SIL4 safety certification behavior
	•	Multi-node cluster coordination across many simulators
	•	Full EN 50701 compliance verification (only core aspects covered)
	•	Advanced AI anomaly detection (if future feature)
1.5 Test Approach
	•	Unit Tests:
	◦	C++ unit tests for ConfigManager, DataSetManager, marshaling/demarshaling, BackendApi utilities.
	◦	Framework: gtest / Catch2.
	•	Integration Tests:
	◦	PD/MD engines with real TRDP stack.
	◦	XML → runtime → TRDP stack → network.
	•	System Tests:
	◦	Full TRDP Simulator with Web UI using real XML examples (like the ones you pasted).
	◦	Communicating with a real TRDP device or another instance of the simulator.
	•	Performance & Stress Tests:
	◦	PD cycles down to 1 ms (where feasible).
	◦	Large numbers of PD telegrams & MD sessions.
	•	Security Tests:
	◦	Authentication & authorization scenarios.
	◦	Invalid payload & XML injection tests.
	◦	Basic DoS scenarios (flooding PD/MD/HTTP).
1.6 Entry Criteria
	•	SRS and SAS baselined.
	•	DDS available and core modules implemented: ConfigManager, PdEngine, MdEngine, TrdpAdapter stubbed to real TRDP stack, BackendApi, minimal Web UI.
1.7 Exit Criteria
	•	All High severity defects fixed.
	•	≥ 95% of prioritized test cases executed & passed.
	•	PD/MD basic interoperability verified against at least one external TRDP device or another simulator instance.
	•	No open security issues blocking deployment.
1.8 Test Environment
	•	Platform A:
	◦	Debian/Ubuntu x86_64 (bare metal or VM).
	•	Platform B:
	◦	Raspberry Pi 4/5, Raspberry Pi OS (ARM).
Network:
	•	Local LAN with switch, multicast enabled.
	•	At least one peer TRDP device or simulator instance.
Tools:
	•	Wireshark with TRDP dissector (if available).
	•	gtest/Catch2 for unit tests.
	•	curl/Postman for API tests.
	•	Modern browser (Chrome/Firefox) for UI tests.



2. Software Test Design (STD) – Key Test Cases
I’ll define representative test cases you can extend. Give them IDs like PD-001, MD-010, etc.
2.1 PD Test Cases
TC: PD-001 – PD Publish Basic Multicast
	•	Objective: Verify PD publisher sends cyclic PD telegrams on configured multicast address.
	•	Preconditions:
	◦	Simulator running on eth0, XML with publish_tlg10001 to 239.255.1.1 cycle 10ms.
	•	Steps:
	◦	Start simulator with sample XML (your PD XML).
	◦	Enable PD comId=10001 via /api/pd/10001/enable ({"enabled": true}).
	◦	Start Wireshark capture on eth0 filter: ip.dst == 239.255.1.1 && udp.port == 17224.
	◦	Observe packets for 5 seconds.
	•	Expected Result:
	◦	PD packets with comId=10001 visible on Wireshark at ~10ms interval.
	◦	/api/pd shows txCount increasing, direction="PUBLISH", no timeouts.



TC: PD-002 – PD Subscribe Basic Multicast
	•	Objective: Verify PD subscriber updates dataset on incoming PD.
	•	Preconditions:
	◦	Another TRDP source or simulator sending comId=1001 to 239.255.1.1.
	◦	Simulator configured as subscriber for same comId/datasetId.
	•	Steps:
	◦	Start simulator.
	◦	Check /api/pd for subscriber telegram comId=1001.
	◦	Trigger sending from peer.
	◦	Poll /api/datasets/{datasetId} corresponding to comId=1001.
	•	Expected:
	◦	rxCount increases for that comId.
	◦	Dataset elements show updated value and defined=true.



TC: PD-010 – PD Timeout & Validity Behavior
	•	Objective: Verify validity-behavior="keep" vs "zero".
	•	Preconditions:
	◦	Subscriber PD telegram with timeout=30000us and two variants: one XML with validity-behavior="keep" and one with "zero".
	•	Steps:
	◦	Start simulator; feed one subscription with periodic packets.
	◦	Stop the sender and wait > timeout.
	◦	Query dataset via /api/datasets/{id}.
	•	Expected:
	◦	For "keep": last received values remain; states may show “timed out” flag if you expose it.
	◦	For "zero": relevant fields reset to zero (per spec).



TC: PD-020 – High Load PD Stress
	•	Objective: Check performance with many PD telegrams.
	•	Preconditions:
	◦	XML defining 100+ PD publishers with small cycles.
	•	Steps:
	◦	Start simulator.
	◦	Enable all PD publishers.
	◦	Monitor CPU load, /api/status, PD jitter, and txCount.
	•	Expected:
	◦	Simulator stays responsive.
	◦	No crashes.
	◦	PD jitter within acceptable range as per NFR.



2.2 MD Test Cases
TC: MD-001 – MD Requester/Responder Loopback
	•	Objective: Validate basic MD request/response on the same machine (simulator as both requester and responder).
	•	Preconditions:
	◦	XML with MD telegrams (Mn TCP UC msg, etc.).
	◦	Simulator configured to respond to certain comIds.
	•	Steps:
	◦	Start simulator.
	◦	Create MD session via POST /api/md/sessions with {"comId":1000}.
	◦	Set request dataset via POST /api/md/sessions/{id}/dataset.
	◦	Send request via POST /api/md/sessions/{id}/send.
	◦	Poll /api/md/sessions/{id} until state changes from "WAITING_REPLY".
	•	Expected:
	◦	state transitions: IDLE → REQUEST_SENT → WAITING_REPLY → REPLY_RECEIVED.
	◦	Response dataset matches responder logic.



TC: MD-010 – MD Timeout and Retry
	•	Objective: Verify retries and timeout when responder is not present.
	•	Preconditions:
	◦	Simulator acts as requester only; no responder for given comId.
	◦	MD config with retries=2, reply-timeout set.
	•	Steps:
	◦	Start simulator.
	◦	Create MD session with target comId.
	◦	Send request.
	◦	Monitor /api/md/sessions/{id} over time.
	•	Expected:
	◦	retryCount increments up to configured number.
	◦	Final state TIMEOUT.
	◦	Diagnostic events include MD timeout messages.



TC: MD-020 – MD UDP vs TCP
	•	Objective: Confirm both protocols work per config.
	•	Steps:
	◦	Repeat MD-001 with protocol="UDP", then with protocol="TCP".
	•	Expected:
	◦	Both configurations yield successful request/response flows.



2.3 Dataset & XML Test Cases
TC: DS-001 – Dataset Parsing and UI Representation
	•	Objective: Verify dataset definitions from XML appear correctly in UI.
	•	Preconditions:
	◦	XML with testDS1001, testDS1002, testDS2001, etc.
	•	Steps:
	◦	Start simulator with that XML.
	◦	Call /api/datasets.
	◦	For each dataset, call /api/datasets/{id}.
	•	Expected:
	◦	Names, IDs, element types, array sizes match XML.
	◦	numElements matches element count.



TC: DS-010 – Dataset Outgoing Value Set/Clear/Lock
	•	Objective: Verify all outgoing dataset controls.
	•	Steps:
	◦	Choose an outgoing dataset id.
	◦	Set each element by POST /api/datasets/{id}/elements/{index}.
	◦	Verify /api/datasets/{id} -> defined=true for all; status "active".
	◦	Call POST /api/datasets/{id}/lock with {"locked": true}.
	◦	Attempt further changes; they should be rejected or ignored.
	◦	Clear element and clear-all; check values & defined flags.
	•	Expected:
	◦	After set: all elements defined, status "active".
	◦	When locked=true, backend either rejects updates with error or keeps dataset unchanged.
	◦	Clear & Clear-all behave as expected.



TC: XML-001 – Invalid XML Handling
	•	Objective: Ensure invalid XML is gracefully reported.
	•	Steps:
	◦	Provide XML with missing required attributes or duplicated dataset IDs.
	◦	Start simulator or call /api/config/reload.
	•	Expected:
	◦	Error logged in diagnostics.
	◦	System either refuses to start or keeps previous valid config.
	◦	API returns error JSON ({"ok": false, "error": "...details..."}).



2.4 Web API & UI Test Cases
TC: API-001 – JSON Schema Conformance
Pick each main endpoint:
	•	/api/datasets, /api/datasets/{id}, /api/pd, /api/md/sessions, /api/config/summary, /api/diag/events, /api/status.
For each:
	•	Steps:
	◦	Call with curl/Postman.
	◦	Validate JSON fields, types, and names against the schemas we defined.
	•	Expected:
	◦	JSON keys exist and types are correct.
	◦	No extraneous keys that confuse frontend.



TC: UI-001 – Dataset Table UI
	•	Objective: Verify the dataset table UI renders correctly in browser.
	•	Steps:
	◦	Open the web UI in Chrome/Firefox.
	◦	Navigate to Dataset view.
	◦	Compare rendered table with /api/datasets/{id} JSON.
	•	Expected:
	◦	Column names match element names.
	◦	Values updated when PD received or when user sets them for outgoing dataset.



2.5 Performance & Stress Test Cases
TC: PERF-001 – PD Load Stress
As in PD-020 but with concrete metrics:
	•	Log CPU usage, jitter, packet drop count (via Wireshark or counters).
TC: PERF-010 – MD Session Concurrency
	•	Create 100+ MD sessions to a peer, send bursts, measure RTT and failure rate.



2.6 Security Test Cases
(These tie into STRIDE below.)
TC: SEC-001 – Authentication Required
	•	Try accessing /api/pd without login / auth token.
	•	Expected: 401 Unauthorized or redirect to login (depending on your design).
TC: SEC-010 – Role-based Access
	•	Viewer user: must not be able to call /api/config/reload or modify datasets.
	•	Admin user: can perform all operations.
TC: SEC-020 – Input Validation (Dataset Values)
	•	Try sending out-of-range values (e.g., negative for UINT, too-long strings for array size).
	•	Expected: API rejects with clear error; no crash.
TC: SEC-030 – Basic DoS Scenario
	•	Send high-rate HTTP requests to /api/datasets and /api/pd.
	•	Expected:
	◦	Server remains responsive.
	◦	No crash, memory leak, or unbounded queues.



3. Software Test Report (STR) – Template
When you execute tests, log them like this.
3.1 STR Structure
	•	Project: TRDP Simulator
	•	Version under Test: vX.Y.Z
	•	Test Period: [start date – end date]
	•	Test Environment: (machine, OS, TRDP version, etc.)
	•	Tester(s): Names
3.1.1 Summary
	•	Number of test cases planned: N
	•	Number executed: N₁
	•	Passed: N₂
	•	Failed: N₃
	•	Blocked: N₄
	•	Overall result: [Pass/Conditional Pass/Fail]
3.1.2 Detailed Results Table
Test ID
Title
Result
Defect ID
Notes
PD-001
PD Publish Basic Multicast
Pass
–
Works at 10ms, jitter < 1ms
PD-010
PD Timeout & Validity Behavior
Fail
BUG-123
ZERO behavior not applied
MD-001
MD Requester/Responder Loopback
Pass
–
–
SEC-020
Input Validation Dataset Values
Pass
–
Rejects invalid UINT16
3.1.3 Defect List
For each bug:
	•	Defect ID: BUG-123
	•	Summary: PD ZERO validity behavior ignored after timeout
	•	Found in: PD-010
	•	Severity: High/Medium/Low
	•	Status: Open / Fixed / Verified
	•	Fix Version: vX.Y.Z



4. Cybersecurity Threat Model (STRIDE-based)
We’ll look at core assets and apply STRIDE:
	•	Spoofing
	•	Tampering
	•	Repudiation
	•	Information disclosure
	•	Denial of service
	•	Elevation of privilege
4.1 Assets & Trust Boundaries
Assets
	•	TRDP PD/MD traffic
	•	Dataset values (could represent safety-critical train states)
	•	XML config files (contain topology, IPs, comIds, data mapping)
	•	Authentication credentials (admin/dev/viewer accounts)
	•	Logs and diagnostic data
	•	Web API itself
Trust Boundaries
	1	Web UI ↔ Web Server: HTTP/HTTPS boundary.
	2	Web Server ↔ TRDP Engine: internal process boundary (trusted) but still needs validation.
	3	TRDP Network ↔ Other devices: IP boundary; other devices may be misconfigured or malicious.
	4	Config Files ↔ Filesystem: user may provide crafted XML.

