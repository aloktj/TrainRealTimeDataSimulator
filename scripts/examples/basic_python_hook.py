"""Example Python hook for scripting_hooks.py.

Define a run(client) function that returns a list of StepResult or
mappings with name/status/details fields.
"""
from typing import List

from scripting_hooks import SimulatorClient, StepResult


def run(client: SimulatorClient) -> List[StepResult]:
    results: List[StepResult] = []

    pd_status = client.get_pd_status()
    results.append(StepResult("fetch pd status", "pass", pd_status))

    md_resp = client.md_request(3001)
    session_id = md_resp.get("sessionId") if isinstance(md_resp, dict) else None
    if session_id:
        md_status = client.wait_for_md(session_id, timeout=2.0)
        results.append(StepResult("wait for md response", "pass", md_status))
    else:
        results.append(StepResult("wait for md response", "fail", {"error": "no session created"}))

    return results
