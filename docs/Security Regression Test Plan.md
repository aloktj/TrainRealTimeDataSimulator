TRDP Simulator – Security Regression Test Plan

Version 1.0 • Based on STRIDE Threat Model & Security Requirements (SR-01…SR-10)
Document Owner: Security & Systems Engineering
Last Updated: 2025


---

1. Introduction

This Security Regression Test Plan ensures that every change to the TRDP Simulator does not reintroduce security vulnerabilities. It is tightly derived from:

STRIDE Cybersecurity Threat Model

Security Requirements (SR-01 to SR-10)

TRDP Simulator Architecture (SAS) and SRS

Test Design (STD) & Security Test Plan (STP)


The goal is to continuously verify:

Authentication & Authorization

Input Validation

XML Hardening

TRDP Network Robustness

DoS Resilience

Logging & Audit

Memory Safety



---

2. Security Test Classification

Security tests are organized into four tiers:

Tier	Name	Frequency	Purpose

S-SMOKE	Security Smoke Suite	Every commit/MR	Catch obvious breakages fast
S-RELEASE	Release Security Regression	Every Release Candidate	Full security check before delivery
S-NIGHTLY	Nightly Deep Security Suite	Daily	Deeper validation + fuzzing
S-ONDEMAND	Campaign/Heavy Tests	Manual	Long fuzz/pen tests



---

3. Test Inventory

Security test IDs used throughout:

Authentication & Roles

SEC-001 – Authentication required

SEC-002 – Role separation (Viewer/Dev/Admin)

SEC-003 – Password storage & brute-force mitigation

SEC-004 – Unauthorized data/config viewing


Input Validation

DATA-001 – Type/range validation

DATA-002 – Lock enforcement

DATA-003 – Clear/Clear-all validation


XML Hardening

XML-001 – Valid XML load

XML-002 – Invalid XML rejected

XML-003 – Duplicate/invalid IDs

XML-004 – Config exposure rules

XML-005 – XML DoS resilience

XML-006 – Command injection protection


TRDP Network Hardening

NET-001 – Source IP awareness / filtering

NET-002 – Malformed PD dropped

NET-004 – Malformed MD dropped


DoS Resilience

DOS-HTTP-001 – HTTP flood resistance

DOS-PD-001 – PD traffic flood resistance

DOS-MD-001 – MD session storm resistance


Logging & Audit

LOG-001 – Audit logging completeness

LOG-002 – Denied operation logging

LOG-004 – No sensitive over-logging


Fuzzing

FUZ-001 – PD fuzz (sanitizer build)

FUZ-002 – MD fuzz (sanitizer build)



---

4. Security Regression Tiers & Coverage


---

4.1 Tier 1 – S-SMOKE (Every Commit / MR)

These tests run in seconds to a few minutes.

Included Test IDs:

Authentication / Authorization

SEC-001 – Authentication required

SEC-002 (basic subset) – Viewer cannot modify; Admin can


Input Validation

DATA-001 (subset) – Basic invalid dataset writes

DATA-002 – Lock prevents write

DATA-003 – Clear/Clear-all basic functionality


XML Safety

XML-001 – Valid XML load

XML-002 (basic) – Missing attribute rejected


Logging

LOG-001 (subset) – Logs login + one write + one failed login


Purpose

Protect against common regressions

Confirm that basic security controls always work

Quick feedback to developers



---

4.2 Tier 2 – S-RELEASE (Every RC / Release Build)

Executed before publishing a release or delivering to customer.

Includes all S-SMOKE tests plus:

Authentication & Permissions

SEC-003 – Password storage (hashes), failure throttling

SEC-004 – Unauthorized access blocked


Input Validation (full)

DATA-001 – Full type/range checks

DATA-003 – Array boundaries, invalid sizes


XML Hardening

XML-003 – Duplicate ID / invalid values

XML-004 – Config summary access matches role

XML-006 – No command execution


TRDP Hardening

NET-001 – Source IP tracking

NET-002 – Malformed PD dropped

NET-004 – Malformed MD dropped


DoS (moderate)

DOS-HTTP-001 – Moderate HTTP RPS

DOS-PD-001 – PD flood (moderate)

DOS-MD-001 – MD storm (moderate concurrency)


Logging & Audit

LOG-001 (full) – All events logged

LOG-002 – Denied operation logged

LOG-004 – Sensitive data not leaked in logs


Purpose

Ensures the release is secure for deployment into lab or test environments.


---

4.3 Tier 3 – S-NIGHTLY (Daily Deep Tests)

Executed automatically every night.

Includes all S-RELEASE plus:

DoS (heavy)

DOS-HTTP-001 (stress) – High RPS flood

DOS-PD-001 (stress) – High multicast storm

DOS-MD-001 (max) – Large MD concurrency


Fuzzing (sanitizer build)

FUZ-001 – PD fuzz (time-boxed)

FUZ-002 – MD fuzz (time-boxed)


XML DoS

XML-005 – Large / deep XML

Confirm early rejection

No CPU or memory exhaustion



Purpose

Detect hidden memory issues

Detect long-run stability problems

Early detection of attack-pattern regressions



---

4.4 Tier 4 – S-ONDEMAND (Manual / Campaign Tests)

Triggered for:

Major architectural changes

Before customer deployment

Before adding new TRDP features

Before interfacing with critical systems


Includes:

Extended fuzzing (hours, not minutes)

Extended network attack simulations

Manual penetration testing

Password/role policy review

Static + dynamic security scanning

Dependency security checks (NVD/CVE scans)


Purpose

Full campaign mode

Stress test every security boundary

Pre-certification security assessment



---

5. CI Integration (Recommended)

Below is a typical CI structure for GitHub/GitLab.

stages:
  - build
  - sec_smoke
  - sec_release
  - sec_nightly

sec_smoke:
  stage: sec_smoke
  script:
    - run-security-smoke.sh
  only:
    - merge_requests
    - pushes

sec_release:
  stage: sec_release
  script:
    - run-security-release.sh
  only:
    - tags
    - release

sec_nightly:
  stage: sec_nightly
  script:
    - run-security-nightly.sh
  only:
    - schedules


---

6. Minimal Always-Pass Suite

Regardless of tier, the following must always pass:

SEC-001 (auth required)

SEC-002 (subset) (role separation)

DATA-001 (subset) (input validation)

XML-001 / XML-002 (valid/invalid XML handling)

NET-002 (malformed PD dropped)

DOS-HTTP-001 (low level) (no trivial DoS crash)

LOG-001 (audit entries present)


If any of these fail, security regression is considered critical/blocker.


---

7. Traceability Matrix (STRIDE → SR → Test Suite)

STRIDE Category	Security Requirement(s)	Regression Tier	Test IDs

Spoofing	SR-01, SR-02, SR-08	S-SMOKE, S-RELEASE	SEC-001, SEC-002, SEC-003
Tampering	SR-03, SR-04, SR-05	All tiers	DATA-001, XML-002, NET-002
Repudiation	SR-07	S-RELEASE	LOG-001, LOG-002
Info Disclosure	SR-01, SR-02, SR-04	S-SMOKE, S-RELEASE	SEC-004, XML-004
DoS	SR-06	S-RELEASE, S-NIGHTLY	DOS-HTTP-001, DOS-PD-001
Elevation of Privilege	SR-02, SR-10	All tiers	SEC-002, FUZ-001



---

8. Exit Criteria for Release Readiness

A release must not ship unless:

1. S-SMOKE = 100% PASS


2. S-RELEASE = 0 critical failures


3. All High-priority test cases pass


4. No unreviewed findings from Nightly fuzzing


5. No new security vulnerabilities introduced since last release




---

9. Change Management

Whenever the following change types occur, expand the suite:

New APIs

New XML fields

New PD/MD features

New roles/permissions

Updated TRDP stack version

New network capabilities (redundancy, SDTv2, etc.)

Backend refactor

Dependency upgrades (Drogon, nlohmann-json, etc.)



---

10. Document Control

Field	Value

Document Name	Security Regression Test Plan
Version	1.0
Owner	Systems & Security Engineering
Status	Approved
Last Update	2025


