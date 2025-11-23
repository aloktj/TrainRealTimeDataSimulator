#!/usr/bin/env python3
"""
Scenario runner and scripting hooks for the TRDP simulator.

Supports JSON-defined scenarios as well as Python or Lua scripts that
call into the simulator's HTTP API. Produces a pass/fail report and can
export recent diagnostic logs alongside the results.
"""

import argparse
import importlib.util
import json
import os
import shutil
import subprocess
import sys
import time
import urllib.error
import urllib.parse
import urllib.request
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Dict, List, Optional


@dataclass
class StepResult:
    name: str
    status: str
    details: Any = None

    def to_json(self) -> Dict[str, Any]:
        payload = {"name": self.name, "status": self.status}
        if self.details is not None:
            payload["details"] = self.details
        return payload


class SimulatorClient:
    """Minimal HTTP client for the simulator API."""

    def __init__(self, base_url: str) -> None:
        self.base_url = base_url.rstrip("/")

    def _request(self, method: str, path: str, payload: Optional[Dict[str, Any]] = None) -> Any:
        url = f"{self.base_url}{path}"
        data = None
        headers: Dict[str, str] = {}
        if payload is not None:
            data = json.dumps(payload).encode("utf-8")
            headers["Content-Type"] = "application/json"
        req = urllib.request.Request(url, data=data, method=method, headers=headers)
        try:
            with urllib.request.urlopen(req) as resp:
                body = resp.read().decode("utf-8")
                if not body:
                    return None
                try:
                    return json.loads(body)
                except json.JSONDecodeError:
                    return body
        except urllib.error.HTTPError as exc:
            raise RuntimeError(f"HTTP {exc.code} for {path}: {exc.read().decode('utf-8')}") from exc
        except urllib.error.URLError as exc:
            raise RuntimeError(f"Failed to reach simulator at {url}: {exc.reason}") from exc

    def get_pd_status(self) -> Any:
        return self._request("GET", "/api/pd/status")

    def enable_pd(self, com_id: int, enabled: bool) -> Any:
        return self._request("POST", f"/api/pd/{com_id}/enable", {"enabled": enabled})

    def get_dataset(self, data_set_id: int) -> Any:
        return self._request("GET", f"/api/datasets/{data_set_id}")

    def set_dataset_element(self, data_set_id: int, element: int, raw: List[int]) -> Any:
        return self._request("POST", f"/api/datasets/{data_set_id}/elements/{element}", {"raw": raw})

    def clear_dataset_element(self, data_set_id: int, element: int) -> Any:
        return self._request("POST", f"/api/datasets/{data_set_id}/elements/{element}", {"clear": True})

    def clear_dataset(self, data_set_id: int) -> Any:
        return self._request("POST", f"/api/datasets/{data_set_id}/clear_all")

    def md_request(self, com_id: int) -> Any:
        return self._request("POST", f"/api/md/{com_id}/request")

    def md_status(self, session_id: int) -> Any:
        return self._request("GET", f"/api/md/session/{session_id}")

    def wait_for_md(self, session_id: int, timeout: float = 5.0, poll_interval: float = 0.2) -> Any:
        deadline = time.time() + timeout
        last = None
        while time.time() < deadline:
            last = self.md_status(session_id)
            if not isinstance(last, dict):
                break
            state = last.get("state")
            if state in {"REPLY_RECEIVED", "WAITING_ACK", "TIMEOUT", "ERROR"}:
                return last
            time.sleep(poll_interval)
        return last

    def export_logs(self, destination: Path, max_events: int = 200, as_json: bool = False) -> Path:
        url = f"/api/diag/log/export?max={max_events}"
        if as_json:
            url += "&format=json"
        resp = self._request("GET", url)
        destination.parent.mkdir(parents=True, exist_ok=True)
        mode = "w"
        data = resp
        if isinstance(resp, (dict, list)):
            data = json.dumps(resp, indent=2)
        if isinstance(resp, bytes):
            mode = "wb"
            data = resp
        with destination.open(mode) as handle:
            handle.write(data)  # type: ignore[arg-type]
        return destination


def _coerce_raw(value: Any) -> List[int]:
    if isinstance(value, list):
        return [int(v) for v in value]
    if isinstance(value, str):
        value = value.strip()
        if value.startswith("0x"):
            value = value[2:]
        if len(value) % 2:
            raise ValueError("hex payload must have an even number of characters")
        raw = []
        for i in range(0, len(value), 2):
            raw.append(int(value[i : i + 2], 16))
        return raw
    raise ValueError("raw payload must be a list of integers or a hex string")


def run_json_scenario(client: SimulatorClient, scenario: Dict[str, Any]) -> List[StepResult]:
    steps: List[StepResult] = []
    for idx, step in enumerate(scenario.get("steps", [])):
        name = step.get("name") or f"step_{idx}"
        action = step.get("action")
        try:
            if action == "pd_enable":
                client.enable_pd(int(step["comId"]), bool(step.get("enabled", True)))
                steps.append(StepResult(name, "pass", {"comId": step["comId"], "enabled": step.get("enabled", True)}))
            elif action == "dataset_set":
                raw = _coerce_raw(step["raw"])
                resp = client.set_dataset_element(int(step["dataSetId"]), int(step["element"]), raw)
                steps.append(StepResult(name, "pass", resp))
            elif action == "dataset_clear":
                if "element" in step:
                    resp = client.clear_dataset_element(int(step["dataSetId"]), int(step["element"]))
                else:
                    resp = client.clear_dataset(int(step["dataSetId"]))
                steps.append(StepResult(name, "pass", resp))
            elif action == "md_request":
                resp = client.md_request(int(step["comId"]))
                session_id = resp.get("sessionId") if isinstance(resp, dict) else None
                wait_ms = step.get("waitMs")
                status_payload = resp
                if session_id and wait_ms:
                    status_payload = client.wait_for_md(session_id, timeout=wait_ms / 1000.0)
                expect_state = step.get("expectState")
                if expect_state and isinstance(status_payload, dict):
                    state = status_payload.get("state")
                    if state != expect_state:
                        raise RuntimeError(f"MD state {state} did not match expected {expect_state}")
                steps.append(StepResult(name, "pass", status_payload))
            elif action == "assert_dataset":
                ds = client.get_dataset(int(step["dataSetId"]))
                expected_hex = step.get("expectHex")
                if expected_hex and isinstance(ds, dict):
                    hex_values = "".join(cell.get("rawHex", "") for cell in ds.get("values", []))
                    if expected_hex.lower() not in hex_values.lower():
                        raise RuntimeError(f"Expected hex sequence {expected_hex} not found in dataset")
                steps.append(StepResult(name, "pass", ds))
            elif action == "sleep":
                time.sleep(float(step.get("seconds", 1)))
                steps.append(StepResult(name, "pass", {"slept": step.get("seconds", 1)}))
            else:
                raise RuntimeError(f"Unsupported action '{action}'")
        except Exception as exc:  # noqa: BLE001
            steps.append(StepResult(name, "fail", {"error": str(exc)}))
    return steps


def _load_python_hook(path: Path, client: SimulatorClient) -> List[StepResult]:
    spec = importlib.util.spec_from_file_location("scenario_hook", path)
    if not spec or not spec.loader:
        raise RuntimeError(f"Unable to load Python hook at {path}")
    module = importlib.util.module_from_spec(spec)
    sys.modules["scenario_hook"] = module
    spec.loader.exec_module(module)  # type: ignore[arg-type]
    if not hasattr(module, "run"):
        raise RuntimeError("Python hook must define a run(client) function")
    raw_results = module.run(client)
    results: List[StepResult] = []
    for idx, item in enumerate(raw_results):
        if isinstance(item, StepResult):
            results.append(item)
            continue
        if isinstance(item, dict):
            results.append(StepResult(item.get("name", f"hook_step_{idx}"), item.get("status", "pass"), item.get("details")))
            continue
        raise RuntimeError("Hook returned an unsupported result type")
    return results


def _run_lua_hook(path: Path, base_url: str) -> List[StepResult]:
    lua_bin = shutil.which("lua") or shutil.which("lua5.4")
    if not lua_bin:
        raise RuntimeError("Lua interpreter not found; install lua or lua5.4 to run Lua hooks")
    env = os.environ.copy()
    env["SIM_BASE_URL"] = base_url
    proc = subprocess.run([lua_bin, str(path)], capture_output=True, text=True, env=env)
    if proc.returncode != 0:
        raise RuntimeError(f"Lua hook failed: {proc.stderr.strip() or proc.stdout.strip()}")
    try:
        parsed = json.loads(proc.stdout)
    except json.JSONDecodeError as exc:  # noqa: PERF203
        raise RuntimeError("Lua hook did not emit JSON results") from exc
    results: List[StepResult] = []
    for idx, item in enumerate(parsed if isinstance(parsed, list) else []):
        if isinstance(item, dict):
            results.append(StepResult(item.get("name", f"lua_step_{idx}"), item.get("status", "pass"), item.get("details")))
        else:
            results.append(StepResult(f"lua_step_{idx}", "fail", {"error": "malformed Lua result"}))
    return results


def build_report(name: str, base_url: str, steps: List[StepResult], log_path: Optional[Path]) -> Dict[str, Any]:
    passed = sum(1 for s in steps if s.status == "pass")
    failed = sum(1 for s in steps if s.status != "pass")
    return {
        "name": name,
        "baseUrl": base_url,
        "summary": {"passed": passed, "failed": failed, "total": len(steps), "timestamp": time.time()},
        "steps": [s.to_json() for s in steps],
        "logs": str(log_path) if log_path else None,
    }


def write_report(report: Dict[str, Any], json_path: Path, text_path: Optional[Path] = None) -> None:
    json_path.parent.mkdir(parents=True, exist_ok=True)
    json_path.write_text(json.dumps(report, indent=2))
    if text_path:
        lines = [f"Scenario: {report['name']}", f"Base URL: {report['baseUrl']}", ""]
        for step in report["steps"]:
            status = step.get("status", "")
            prefix = "PASS" if status == "pass" else "FAIL"
            lines.append(f"[{prefix}] {step.get('name')}")
            if step.get("details"):
                lines.append(f"    details: {json.dumps(step['details'])}")
        lines.append("")
        summary = report.get("summary", {})
        lines.append(
            f"Passed: {summary.get('passed', 0)}  Failed: {summary.get('failed', 0)}  Total: {summary.get('total', 0)}",
        )
        if report.get("logs"):
            lines.append(f"Logs exported to: {report['logs']}")
        text_path.parent.mkdir(parents=True, exist_ok=True)
        text_path.write_text("\n".join(lines))


def parse_args(argv: Optional[List[str]] = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Run scripted simulator scenarios")
    parser.add_argument("--base-url", default="http://127.0.0.1:8000", help="Simulator base URL")
    parser.add_argument("--json-scenario", type=Path, help="Path to JSON scenario description")
    parser.add_argument("--python-hook", type=Path, help="Path to Python hook implementing run(client)")
    parser.add_argument("--lua-hook", type=Path, help="Path to Lua hook that prints JSON results")
    parser.add_argument(
        "--report",
        type=Path,
        default=Path("reports/automation_report.json"),
        help="Path for JSON report output",
    )
    parser.add_argument("--report-text", type=Path, help="Optional human-readable report path")
    parser.add_argument("--export-logs", type=Path, help="Export recent diagnostic logs to this path")
    parser.add_argument("--log-max-events", type=int, default=200, help="Number of diagnostic events to export")
    parser.add_argument("--log-json", action="store_true", help="Export logs as structured JSON instead of text")
    return parser.parse_args(argv)


def main(argv: Optional[List[str]] = None) -> int:
    args = parse_args(argv)
    if not any([args.json_scenario, args.python_hook, args.lua_hook]):
        print("Provide at least one of --json-scenario, --python-hook, or --lua-hook", file=sys.stderr)
        return 2

    client = SimulatorClient(args.base_url)
    all_steps: List[StepResult] = []
    scenario_name = "scripted-run"

    if args.json_scenario:
        scenario = json.loads(args.json_scenario.read_text())
        scenario_name = scenario.get("name", args.json_scenario.stem)
        all_steps.extend(run_json_scenario(client, scenario))

    if args.python_hook:
        hook_steps = _load_python_hook(args.python_hook, client)
        all_steps.extend(hook_steps)

    if args.lua_hook:
        hook_steps = _run_lua_hook(args.lua_hook, args.base_url)
        all_steps.extend(hook_steps)

    log_path = None
    if args.export_logs:
        log_path = client.export_logs(args.export_logs, args.log_max_events, args.log_json)

    report = build_report(scenario_name, args.base_url, all_steps, log_path)
    write_report(report, args.report, args.report_text)

    if any(step.status != "pass" for step in all_steps):
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
