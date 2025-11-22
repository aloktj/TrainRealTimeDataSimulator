Software Requirements Specification (SRS)
TRDP Simulator Software for Linux & Raspberry Pi
Version 1.0



1. Introduction
1.1 Purpose
The purpose of the TRDP Simulator is to provide a comprehensive, configurable, industrial-grade simulation of a TRDP (Train Real-time Data Protocol) end device. The simulator shall:
	•	Emulate PD (Process Data) and MD (Message Data) communication
	•	Load configuration using standard TRDP XML
	•	Provide a web-based HMI for monitoring, editing, and controlling TRDP telegrams
	•	Support Linux (x86_64, VM) and Raspberry Pi (ARM)
	•	Serve as a test, verification, debugging, training, and integration tool for rail OEMs and integrators
This SRS defines the complete set of functional and non-functional requirements.



1.2 Scope
The TRDP Simulator software will:
	•	Act as a TRDP Publisher, Subscriber, MD Requester, MD Responder
	•	Support all unicast/multicast TRDP configurations
	•	Support IEC 61375-2-3 TRDP standard behavior
	•	Provide an industrial-grade web interface
	•	Support diagnostics, logging, scripting, and safety-critical simulation features
	•	Meet the needs of railway developers, integrators, testers, and maintainers
Users include:
	•	TCMS integrators
	•	Rail OEM development teams
	•	Railway testing labs
	•	Embedded system developers
	•	Automation engineers



1.3 Definitions, Acronyms
TRDP – Train Real-time Data Protocol PD – Process Data (cyclic telegrams) MD – Message Data (request/response) ECN – Ethernet Consist Network TCMS – Train Control & Monitoring System SIL – Safety Integrity Level SDT – Safe Data Transmission XML – Extensible Markup Language



1.4 References
	•	IEC 61375-2-3 (TRDP)
	•	IEC 61375-2-5 (Consist Network)
	•	IEC 61375-2-1 (TCN-ECN)
	•	EN 50701 (Rail Cybersecurity)
	•	TCNOpen TRDP Stack Documentation
	•	POSIX Linux system references



1.5 Overview
This SRS defines:
	•	System requirements
	•	Functional requirements
	•	Non-functional requirements
	•	Safety & cybersecurity requirements
	•	Rail-industry enhancements
	•	Constraints and future extensions



2. Overall Description
2.1 Product Perspective
The TRDP Simulator is a standalone software system composed of:
	1	TRDP Backend Engine
	2	Web Server (REST + WebSocket)
	3	Industrial Web User Interface
	4	XML Configuration Loader & Validator
	5	Diagnostics, Logging, & Scripting Engine
It interacts with:
	•	Network interfaces (eth0, eth1, etc.)
	•	XML configuration files
	•	Other TRDP-enabled devices on the network
	•	User’s browser



2.2 Product Functions
High-level functions include:
	•	Parsing TRDP XML
	•	Managing bus interfaces, com-parameters, datasets, telegrams
	•	Handling PD/MD communication lifecycle
	•	Dataset monitoring and editing
	•	Real-time Web UI display of telegrams
	•	Diagnostics and logging
	•	Scripting and automation
	•	Rail-industrial advanced testing features



2.3 User Characteristics
Target users:
	•	Rail system integrators
	•	Developers familiar with TRDP
	•	Test engineers
	•	Maintenance operators
	•	Safety engineers
Users are expected to have basic understanding of TRDP telegrams, datasets, and IP networking.



2.4 Constraints
	•	Must use TCNOpen TRDP stack
	•	Must comply with TRDP XML schema
	•	Must run on Linux x86_64 or Raspberry Pi ARM
	•	Web-based UI only (no desktop GUI)
	•	Real-time execution constraints for PD cycles
	•	Must not violate EN 50701 cybersecurity guidelines



2.5 Assumptions
	•	System runs on a trusted internal rail lab network
	•	Users know how to configure XML files
	•	Valid NICs and multicast routing are available



3. System Requirements



3.1 Functional Requirements (FR)
FR-1: XML Configuration Management
The system shall:
	•	Parse TRDP XML conforming to official TCNOpen schema
	•	Validate XML structure
	•	Display parsed configuration in UI
	•	Allow reloading XML at runtime
	•	Handle device-configuration, com-parameters, datasets, telegrams, interfaces, mapped devices
	•	Report XML errors with line number and description



FR-2: TRDP PD Communication
The system shall support:
FR-2.1 PD Publisher
	•	Send cyclic PD telegrams
	•	Configurable cycle time
	•	Support unicast and multicast
	•	Apply marshalling settings
	•	Support redundant PD configurations
	•	Support validity behavior (zero/keep)
	•	Provide manual "send now" triggers
	•	Provide publish enable/disable
FR-2.2 PD Subscriber
	•	Receive PD packets from network
	•	Decode datasets
	•	Display values in UI
	•	Show validity, timeout, jitter, sequence number
FR-2.3 PD Status Monitoring
	•	Show telegram counters
	•	Show cycle-time jitter
	•	Show timeout status
	•	Show RX/TX timestamps
	•	Show SDT parameters



FR-3: TRDP MD Communication
The system shall support:
FR-3.1 MD Requester
	•	Initiate MD requests
	•	Handle TCP or UDP according to XML
	•	Display response
	•	Show timeout and retry info
FR-3.2 MD Responder
	•	Respond to MD requests
	•	Support multiple parallel sessions
	•	Respect configured protocol (UDP/TCP)
FR-3.3 MD Diagnostics
	•	Show session state
	•	Show round-trip latency
	•	Provide hex/raw/parsed view



FR-4: Dataset Handling
The system shall:
	•	Parse dataset definitions from XML
	•	Dynamically build UI tables
	•	Support all TRDP primitive types: BOOL8, CHAR8, UTF16, INT8/16/32/64, UINT8/16/32/64, REAL32/REAL64, TIMEDATE32/48/64
	•	Support arrays
	•	Support nested data-set types
	•	Validate value ranges during editing
FR-4.1 Dataset UI Behavior
	•	Incoming datasets → read-only
	•	Outgoing datasets → editable
	•	Provide SET, CLEAR, LOCK ALL, CLEAR ALL buttons
	•	Compute dataset status (Active/Inactive)



FR-5: Web Interface
The system shall provide:
	•	Real-time dashboards
	•	PD Publisher/Subscriber panels
	•	MD Requester/Responder panels
	•	Dataset editors
	•	XML visual viewer
	•	Log viewer
	•	Interface diagnostics
	•	Theme switching (Dark/Light)
	•	Mobile/tablet friendly UI
	•	Secure login system (Admin/Dev/Viewer)



FR-6: Multicast & Network Handling
The simulator shall:
	•	Join multicast groups per XML automatically
	•	Allow manual join/leave via UI
	•	Support IPv4 multicast routing
	•	Support multiple NICs (eth0, eth1 etc.)
	•	Work in virtualized NICs (VMWare/VirtualBox/WSL)
	•	Work on Raspberry Pi NICs



FR-7: Diagnostics and Logging
The system shall:
	•	Maintain detailed TRDP logs
	•	Provide packet statistics
	•	Offer PCAP capture export
	•	Offer event browser with timestamps
	•	Provide jitter and latency graphs
	•	Provide error counters (timeouts, retries, invalid packets, XML errors)
	•	Provide health status panel



FR-8: Rail-Industry Test Features (Advanced)
The system shall support:
FR-8.1 Error Injection
	•	Wrong com-id
	•	Wrong dataset-id
	•	Corrupted data fields
	•	Sequence number manipulation
	•	Artificial delays and losses
FR-8.2 Performance Stress Mode
	•	High-load PD generation (up to 1000 telegrams)
	•	MD storm testing
	•	Cycle-time stress down to 1 ms
FR-8.3 Redundancy Simulation
	•	A/B channel switching
	•	Failover tests
	•	Bus failure simulation
FR-8.4 Time Synchronization
	•	NTP/PTP offset display
	•	TRDP timestamp converter
FR-8.5 Multi-Device Simulation
	•	Run multiple virtual devices in same PC
	•	Different XML per instance



FR-9: Automation & Scripting
The system shall:
	•	Support scripting (Python/Lua/JSON)
	•	Allow scripts to send PD/MD
	•	Validate responses
	•	Generate automated Pass/Fail reports



FR-10: Security Requirements
Per EN 50701:
	•	Role-based access control
	•	Admin/Developer/Viewer roles
	•	Password hashing (Argon2/PBKDF2)
	•	Disable public binding of webserver by default
	•	Input sanitization
	•	Buffer overflow safe dataset handling
	•	MD TCP session rate limiting
	•	Log sanitization



FR-11: File I/O Requirements
	•	XML import/export
	•	Log export (txt, json)
	•	PCAP export
	•	Backup/restore of configuration



3.2 Non-Functional Requirements (NFR)
NFR-1: Performance
	•	PD cycle accuracy: ≤ 1 ms jitter on Linux VM, ≤ 5 ms on Raspberry Pi
	•	Support 500+ PD telegrams
	•	Support 200+ MD sessions
	•	Web UI update rate ≥ 10 Hz



NFR-2: Reliability
	•	Auto-recover from NIC down/up
	•	Graceful recovery from XML errors
	•	No crash during malformed dataset decode
	•	TRDP engine must operate independently from UI



NFR-3: Usability
	•	UI must require minimal training
	•	Dataset tables must be intuitive
	•	Diagnostics must use industrial symbolic color coding
	•	Touch-friendly UI



NFR-4: Portability
Support:
	•	x86_64 Linux (bare metal & VMs)
	•	Raspberry Pi ARM (Pi 4/5)
	•	Native build and cross-compilation



NFR-5: Maintainability
	•	Modular structure (XML → TRDP Engine → Interface Manager → UI)
	•	Logging with timestamps & severity
	•	Clean CMake build system



NFR-6: Safety Simulation Behavior
	•	Deterministic PD scheduling
	•	Configurable safe defaults
	•	Validity-behavior simulation (zero/keep)
	•	Redundant telegram simulation



3.3 System Interfaces
3.3.1 Network Interfaces
	•	Bind to multiple NICs
	•	Support unicast/multicast
	•	Support VLANs and virtual NICs
3.3.2 User Interfaces
	•	Browser-based UI
	•	REST API
	•	WebSocket real-time stream
3.3.3 File Interfaces
	•	XML
	•	PCAP
	•	JSON logs



4. Other Requirements
4.1 Packaging & Deployment
	•	Debian (.deb) installer
	•	Docker image
	•	Systemd service file
	•	Raspberry Pi optimized package



4.2 Future Enhancements
	•	MQTT bridge
	•	Cloud dashboard export
	•	Embedded Wireshark viewer
	•	AI-based anomaly detection

