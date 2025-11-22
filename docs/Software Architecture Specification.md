Software Architecture Specification (SAS)
TRDP Simulator Software for Linux & Raspberry Pi
Version 1.0

1. Introduction
1.1 Purpose of this Document
This SAS defines the architectural design of the TRDP Simulator software, covering:
System context


High-level architecture


Module architecture


Component interactions


Threading model


Data flow


Network architecture


Security architecture


Deployment & runtime model


Scalability considerations


It supports the SRS and guides detailed design and implementation.

1.2 System Overview
The TRDP Simulator consists of five major subsystems:
TRDP Backend Engine (C/C++)


Configuration & XML Manager


Web Server Layer (REST + WebSocket)


Industrial Web UI Frontend


Diagnostics & Logging Subsystem


All components integrate to simulate rail TRDP devices according to IEC 61375-2-3.

2. System Context Architecture
The system runs as a single executable hosting:
TRDP Engine


Web Server


Monitoring Panel


XML Loader


2.1 Context Diagram
┌─────────────────────────────────────────────────────────┐
 │                 User Browser (PC/Tablet/Mobile)         │
 │  - Web UI                                                │
 │  - Dashboard / control / dataset editing                │
 └───────────────▲─────────────────────────────────────────┘
                 │ HTTP/REST + WebSocket
 ┌───────────────┴──────────────────────────────────────────┐
 │        TRDP Simulator Software (Single Process)           │
 │                                                           │
 │   ┌──────────────────────────────────────┐                │
 │   │          Web Server Layer            │                │
 │   │ (Drogon / FastAPI / Custom HTTP)     │                │
 │   └───────────────────────▲──────────────┘                │
 │                           │ API Calls / JSON              │
 │   ┌───────────────────────┴───────────────────────────┐   │
 │   │          TRDP Backend Engine (C/C++)              │   │
 │   │  - PD Engine                                      │   │
 │   │  - MD Engine                                      │   │
 │   │  - TRDP Stack Adapter                             │   │
 │   └───────────────────────▲──────────────┘               │
 │                           │ Telegrams / Datasets          │
 │   ┌───────────────────────┴───────────────────────────┐   │
 │   │           XML Configuration Manager               │   │
 │   └───────────────────────▲──────────────┘               │
 │                           │ Dataset models                │
 │   ┌───────────────────────┴───────────────────────────┐   │
 │   │         Diagnostics & Logging Subsystem           │   │
 │   └───────────────────────────────────────────────────┘   │
 │                                                           │
 └───────────────────────────┬───────────────────────────────┘
                             │ UDP/TCP TRDP PD+MD Telegrams
 ┌───────────────────────────▼──────────────────────────────┐
 │               Other TRDP Devices on Network              │
 │ (TCMS nodes, Door control, HVAC, Brake units, etc.)      │
 └──────────────────────────────────────────────────────────┘


3. High-Level System Architecture
The system uses a layered architecture, typical for rail systems.

3.1 Layered Architecture Diagram
┌──────────────────────────────────────────────┐
 │          Industrial Web Interface (UI)       │
 │    - React / HTML5 / tailwind / web socket   │
 └───────────────────────▲──────────────────────┘
                         │ HTTP/REST + WS
 ┌───────────────────────┴──────────────────────┐
 │         Web Server Layer (C++)               │
 │  - REST API Endpoint                         │
 │  - WebSocket endpoints for real-time data    │
 │  - Auth, role management                     │
 └───────────────────────▲──────────────────────┘
                         │ JSON RPC / events
 ┌───────────────────────┴──────────────────────┐
 │      TRDP Backend Engine (Core C/C++)        │
 │  - PD Engine                                 │
 │  - MD Engine                                 │
 │  - TRDP Stack Adapter (TCNOpen)              │
 │  - Dataset Manager                           │
 │  - Interface Manager (NICs)                  │
 └───────────────────────▲──────────────────────┘
                         │ XML models / config
 ┌───────────────────────┴──────────────────────┐
 │    XML Configuration Manager (C++)           │
 │  - XML parser (tinyxml2/libxml)              │
 │  - Validation against XSD                    │
 │  - Model builder                             │
 └───────────────────────▲──────────────────────┘
                         │ logs, alerts
 ┌───────────────────────┴──────────────────────┐
 │ Diagnostics & Logging Subsystem              │
 │ - Event manager                              │
 │ - Log rotation                               │
 │ - PCAP export                                │
 └──────────────────────────────────────────────┘


4. TRDP Backend Engine Architecture
This is the core functional engine.

4.1 Internal Components
PD Engine


Manages cyclic telegrams


Timer-based scheduling


Handles send/receive callbacks


Applies validity rules


Manages redundant PD channels


MD Engine


Manages MD request/response


Controls TCP/UDP channels


Manages session state


Handles retries, timeouts


TRDP Stack Adapter


Wraps TCNOpen APIs:


tlc_init


tlc_publish


tlc_subscribe


tlc_request


tlc_reply


Converts raw telegrams into dataset values


Dataset Manager


Holds data structures for all datasets


Supports nested & array datasets


Manages active/inactive states


Provides thread-safe set/get operations


Interface Manager


Binds TRDP stack to NICs


Multicast join/leave


NIC monitoring



5. Threading Architecture
Rail TRDP simulators must be deterministic.

5.1 Thread Overview
Thread
Responsibility
Main Thread
Startup, XML load, TRDP init
PD Publisher Thread(s)
Timed cyclic publishing
PD Subscriber Callback Thread
PD RX processing
MD Session Thread(s)
MD TX/RX management
Timer Service Thread
microsecond timers
Web Server Threads
HTTP/REST serving
WebSocket Thread
Real-time data push
Diagnostics Thread
Logging, event buffering
Optional Script Engine Thread
Automated test execution


5.2 Thread Synchronization
Use:
Mutex + condition variables


TRDP callback safe queues


Read/write locks for dataset access


Priority inheritance mutexes for real-time constraints



6. Data Flow Design

6.1 PD Data Flow
[XML Loader] → dataset model → [PD Engine] → TRDP API → Network NIC
                                                     ↑
                                          PD RX callback from NIC

6.2 MD Data Flow
User Trigger (UI/Script) → WebServer → MD Engine → TRDP API → Network
                                               ← Response ←

6.3 Web UI Flow
Core Engine → Dataset values → WebSocket → Browser UI → Live tables


7. Module Architecture

7.1 TRDP Engine Modules
7.1.1 PD Publisher Module
Handles all PD telegrams


Timer-driven


Supports redundant A/B telegrams


Implements Safe Data Transmission parameters


7.1.2 PD Subscriber Module
Receives telegrams


Updates dataset manager


Detects timeouts


Maintains sequence numbers


7.1.3 MD Client/Server Modules
TCP or UDP


Supports multiple sessions


Handles retries & confirm timeouts


7.1.4 TRDP Adapter
Normalizes TRDP stack communication


Converts binary payload ↔ dataset structures



7.2 Configuration Manager Modules
7.2.1 XML Parser
Validate XML


Build in-memory objects:


Interfaces


Telegrams


Datasets


com-parameters


7.2.2 Configuration Validator
Ensures XML values are in safe & valid ranges



7.3 Web Server Layer Modules
7.3.1 REST API Layer
Provides endpoints like:
/api/pd/status


/api/md/send


/api/config/load


/api/dataset/set


7.3.2 WebSocket Layer
Pushes:
Live PD values


Live MD status


Error events


Jitter/timing graphs


7.3.3 Authentication Layer
Session management


Role-based access



7.4 Diagnostics Subsystem
7.4.1 Event Logger
Info / Warning / Error / Critical


Rotating logs


7.4.2 Packet Capture Engine
Internal packet tap


Export PCAP files


7.4.3 System Health Monitor
CPU/RAM


Timer drift


Network throughput



8. Security Architecture
Rail cybersecurity requires strict controls.

8.1 Security Layers
Network binding rules


Default bind only to localhost


Allow manual override


Authentication


Admin / Developer / Viewer roles


Password hashing (Argon2/PBKDF2)


Input Sanitization


XML validation


Dataset value type checks


Command rate limiting


MD/TCP Defensive Measures


Limit sessions


Limit payload size


Limit retries


Logging Protection


Do not store plaintext passwords


Rotate logs



9. Deployment Architecture

9.1 Supported Platforms
Linux x86_64
Bare metal


Virtual machines


Raspberry Pi ARM
Raspberry Pi 4 / 5


Raspberry Pi OS / Debian ARM



9.2 Deployment Diagram
┌──────────────────────────────┐
 │    TRDP Simulator .deb       │
 │  Installed via apt/dpkg      │
 └───────────────┬──────────────┘
                 │
      ┌──────────▼──────────┐
      │ Systemd Service     │
      │ trdp-simulator.service │
      └──────────┬──────────┘
                 │
        ┌────────▼────────┐
        │ Executable      │
        │ /usr/bin/trdp   │
        └──────┬─────────┘
               │
        ┌──────▼────────┐
        │ Web UI on     │
        │ http://<ip>:port │
        └──────────────────┘


10. Scalability Architecture
System designed to scale:
Multiple PD telegrams (500+)


Multiple MD sessions (200+)


Multiple simulators running on same network


UI real-time updates via WebSocket


Future: cluster mode with multiple Raspberry Pis.

11. Error Handling & Fault Tolerance
11.1 Fault Handling
XML errors → UI notifications


TRDP stack errors → logged & displayed


NIC down → auto-retry join


Dataset decode error → mark dataset invalid


11.2 Recovery Strategy
Soft restart of TRDP engine without process kill


Auto-refresh of UI state



12. Future Architectural Extensions
Adding MQTT bridge


Cloud dashboard integration


AI-driven anomaly detection for TRDP traffic


Built-in Wireshark-like packet viewer

