# Agent Guidelines

## Project Goal

Build a reliable ESP32-C3 + ML307R SMS forwarding firmware.

Primary priority:

1. Receive SMS reliably.
2. Never lose modem UART data.
3. Queue messages before forwarding.
4. Keep Web status and logs visible.
5. Recover automatically from WiFi, modem, and forwarding failures.

## Architecture Rules

- Use PlatformIO + Arduino-ESP32.
- Keep code modular; do not put business logic in one large `main.cpp`.
- `modem_at` is the only module allowed to read from `Serial1`.
- Other modules must submit modem work through `modem_at`; they must not read, flush, or clear UART data directly.
- SMS receiving must never depend on HTTP, SMTP, Web UI, Ping, or manual AT debugging.
- Received SMS must be parsed and queued first; forwarding happens asynchronously.
- All network operations must have explicit timeouts.
- Long operations must be implemented as poll/state-machine logic where practical.

## Module Boundaries

Expected core modules:

- `modem_at`: UART ownership, AT command queue, response matching, URC dispatch, modem power cycle.
- `sms_receiver`: `+CMT` handling, PDU decode, long SMS merge, blacklist check.
- `sms_queue`: pending SMS storage, retry state, forwarding status.
- `forwarder_http`: HTTP/Webhook delivery, timeout handling, retry/backoff.
- `wifi_manager`: WiFi connect, reconnect, AP provisioning mode.
- `web_server`: status, config, logs, queue view, system actions.
- `logger`: RAM ring log exposed through Web.
- `health_monitor`: modem/WiFi/heap checks and recovery.
- `scheduler`: timed reboot and deferred maintenance tasks.

## Stability Requirements

- Do not call `while (Serial1.available()) Serial1.read()` outside `modem_at`.
- Do not block SMS reception while sending HTTP, SMTP, Ping, or Web responses.
- Do not use unlimited retry loops in setup; expose failure state through logs/status.
- Do not silently drop SMS unless queue-full policy explicitly logs it.
- Every modem reset, WiFi reconnect, forwarding failure, and SMS parse failure must be logged.
- Prefer fixed-size buffers for hot paths; avoid excessive `String` concatenation in long-running logic.

## Testing Rules

Use PlatformIO tests.

Native tests should cover:

- AT line parser.
- AT command state machine.
- URC dispatch.
- SMS queue behavior.
- Retry/backoff policy.
- JSON escaping and URL encoding.
- Long SMS merge logic.

Embedded tests should cover:

- UART `AT -> OK`.
- GPIO modem power cycle.
- `AT+CMGF=0`.
- `AT+CNMI=2,2,0,0,0`.
- `AT+CEREG?`.
- Real SMS receive path when hardware is available.

Before release:

- Run native tests.
- Build firmware successfully.
- Run at least one hardware smoke test.
- For stable releases, run 24-72 hour soak test.

## Release Rules

- Follow `docs/release-process.md` for versioning, changelog, release commits, and Git tags.
- Use Semantic Versioning with `vX.Y.Z` Git tags.
- Use Conventional Commits for commit messages so the release script can generate `CHANGELOG.md`.
- Do not create release commits from feature branches.
- Do not rewrite existing release tags.
- Do not release with failing native tests or failing firmware builds.

## Web UI Rules

- Keep Web UI lightweight and device-admin oriented.
- Prefer LuCI-like forms, tables, and logs.
- Do not use React, Vue, Tailwind, or large frontend frameworks.
- Prefer static HTML/CSS/JS plus JSON APIs.
- Web requests must not directly perform long modem operations; submit tasks and poll status.

## Configuration Rules

- Do not hardcode real WiFi credentials in source.
- Store runtime configuration in NVS/Preferences.
- Support AP provisioning when WiFi is not configured or cannot connect.
- Avoid frequent Flash writes.

## Documentation Rules

- Keep hardware assumptions documented.
- Keep release process changes documented in `docs/release-process.md`.
- Document every AT command used and why it is used.
- Update docs when pin mapping, modem behavior, queue policy, or recovery policy changes.

## Do Not Do

- Do not copy large blocks from the legacy firmware into the new architecture.
- Do not let Web AT debug bypass `modem_at`.
- Do not make SMTP part of the critical SMS receive path.
- Do not add OTA, payment, licensing, or cloud features before the local stable core works.
- Do not optimize UI before SMS receive and forwarding reliability are proven.
