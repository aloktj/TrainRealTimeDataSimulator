# Backlog

Future enhancements to expand the simulator's interoperability and observability:

- **MQTT bridge**: publish PD/MD events and diagnostics to a configurable MQTT broker with per-topic QoS mappings.
- **Cloud dashboard export**: pluggable sink that forwards diagnostic metrics/events to a cloud dashboard (e.g., Grafana/Loki/Datadog) via HTTPS or OpenTelemetry.
- **Embedded Wireshark viewer**: lightweight web UI pane that streams rotating PCAPs and renders decoded TRDP frames using tshark bindings.
- **AI anomaly detection**: optional module that learns PD timing/value baselines and raises diagnostics when outliers or jitter bursts are detected.
