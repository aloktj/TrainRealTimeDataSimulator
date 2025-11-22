# Scripting the TRDP Simulator

The `scripts/scripting_hooks.py` helper adds lightweight automation hooks so
scenarios can be driven from JSON, Python, or Lua while the simulator is
running.

## Running a JSON scenario

```bash
python scripts/scripting_hooks.py \
  --json-scenario scripts/examples/basic_scenario.json \
  --export-logs reports/diag.log \
  --report reports/basic.json \
  --report-text reports/basic.txt
```

A JSON scenario is an object with a `name` and a list of `steps`:

- `pd_enable`: `{ "action": "pd_enable", "comId": 1000, "enabled": true }`
- `dataset_set`: `{ "action": "dataset_set", "dataSetId": 101, "element": 0, "raw": "0x00010203" }`
- `dataset_clear`: `{ "action": "dataset_clear", "dataSetId": 101, "element": 0 }` (omit `element` to clear all)
- `md_request`: `{ "action": "md_request", "comId": 3001, "waitMs": 2000, "expectState": "REPLY_RECEIVED" }`
- `assert_dataset`: `{ "action": "assert_dataset", "dataSetId": 101, "expectHex": "00010203" }`
- `sleep`: `{ "action": "sleep", "seconds": 0.5 }`

Each step is marked pass/fail in the generated report. MD steps can
optionally wait for a response and assert a specific state.

## Python hooks

Pass `--python-hook` to load a Python module that defines a `run(client)`
function. The client is an instance of `scripting_hooks.SimulatorClient`
covering PD, MD, datasets, and log export helpers. The hook should return
`StepResult` instances or dictionaries with `name`, `status`, and
optional `details` fields.

Example: `scripts/examples/basic_python_hook.py`.

## Lua hooks

Lua scripts receive the simulator base URL via the `SIM_BASE_URL` environment
variable and must print a JSON array of `{ name, status, details }` objects.
Use `--lua-hook` to attach the script. An example that shells out to `curl`
can be found at `scripts/examples/basic_lua_hook.lua`.

## Reports and log export

Reports are written in JSON by default and can also be mirrored to a
text file with `--report-text`. Including `--export-logs` downloads the
most recent diagnostic events from `/api/diag/log/export` so automated
runs can bundle simulator logs alongside pass/fail results.
