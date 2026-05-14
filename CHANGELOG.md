# Changelog

All notable changes to this project will be documented in this file.

This changelog is generated from Conventional Commit messages by
`scripts/release.py`.

<!-- releases -->

## [0.2.0] - 2026-05-14

### Features

- sms: add RAM SMS queue (f2affa3)
- wifi: add minimal STA wifi manager (1ed1915)
- push: add Bark HTTP forwarder (cc50797)
- sms: merge concatenated SMS parts (ec6df93)
- logger: add RAM ring buffer logging (322e8f4)
- web: add read-only status APIs (86a8869)
- config: add Preferences-backed config store (d716d6b)
- web: add config API (a403c1a)
- web: add dashboard pages (0b719e9)

### Bug Fixes

- modem: preserve long PDU lines (f25f0ff)
- push: preserve long UTF-8 Bark payloads (b8e24e9)
- sms-queue: release sent items after forwarding (aa246b5)
- sms: handle CMTI stored messages (e0885ec)
- sms: delete all stored concat parts (591315a)

### Refactoring

- modem: centralize AT commands (271aa3f)

### Chores

- log: slow heartbeat output (d6768ce)

### Other Changes

- Merge feature/sms-queue (4767117)
- Merge feature/wifi-manager (d30803e)
- Merge feature/push-forwarder-bark (a97b685)
- Merge feature/fix-long-pdu-line-buffer (fd5795e)
- Merge branch 'feature/sms-concat-merge' (06328c2)
- Merge branch 'feature/logger-ring-buffer' (3e700a9)
- Merge branch 'feature/web-readonly-api' (7067774)
- Merge branch 'feature/web-config-api' (86303f9)
- Merge branch 'feature/web-dashboard' (9222859)
- merge: fix CMTI stored SMS handling (dc2820c)
- merge: centralize modem AT commands (38771e4)


## [0.1.0] - 2026-05-09

### Features

- hardware: add ESP32-C3 hardware smoke test (72d6d9b)
- modem: add AT command queue and URC dispatch core (3417fa7)
- sms: add PDU SMS receiver v0 (7b07915)
- release: publish GitHub releases (cf35f4d)

### Bug Fixes

- release: parse git log entries with empty bodies (ebea351)

### Build

- release: add local release tooling (148b489)

### Chores

- scaffold PlatformIO project (49018be)
- release: ignore python cache files (0ec2e10)

### Other Changes

- Merge feature/sms-receiver-v0 (98f83ad)
- Merge feature/release-tooling (f7bc2c2)
- Merge release tooling fixes (a6ba66f)
- Merge release tooling cache ignore (aa8d5a5)
- Merge feature/github-release-publishing (3e61479)
