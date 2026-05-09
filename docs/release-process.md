# Release Process

This project uses Semantic Versioning, Conventional Commits, and a local release
script to produce repeatable firmware releases.

References:

- Semantic Versioning: https://semver.org/
- Conventional Commits: https://www.conventionalcommits.org/

## Versioning

Firmware versions use SemVer:

```text
MAJOR.MINOR.PATCH
```

Rules:

- `MAJOR`: incompatible public behavior or configuration changes.
- `MINOR`: new backward-compatible functionality.
- `PATCH`: backward-compatible bug fixes.
- During early development, versions stay in `0.x.y`.
- Git tags use a `v` prefix, for example `v0.1.0`.
- The plain version is stored in `VERSION`, for example `0.1.0`.

For this project, public behavior includes:

- SMS receive behavior.
- AT command and modem recovery behavior.
- SMS queue policy.
- HTTP forwarding behavior.
- Web API behavior.
- Runtime configuration format.

## Commit Message Format

Use Conventional Commits:

```text
type(scope): summary
```

Examples:

```text
feat(sms): decode incoming PDU messages
fix(modem): keep CMT URC separate from AT response
docs(hardware): document ML307R LED behavior
test(sms): cover invalid PDU input
build(platformio): add pdulib dependency
chore(release): v0.1.0
```

Common types:

- `feat`: new feature, usually triggers a minor release.
- `fix`: bug fix, usually triggers a patch release.
- `docs`: documentation only.
- `test`: tests only.
- `build`: build system, dependencies, or PlatformIO config.
- `chore`: maintenance tasks.
- `refactor`: internal code change without behavior change.
- `perf`: performance improvement.

Breaking changes may be marked with `!`:

```text
feat(config)!: change stored config format
```

Or with a footer:

```text
BREAKING CHANGE: existing NVS config must be reset
```

## Changelog

`CHANGELOG.md` is generated from commit messages.

Release entries should look like:

```md
## [0.1.0] - 2026-05-09

### Features

- sms: decode incoming PDU messages

### Bug Fixes

- modem: keep CMT URC separate from AT response

### Documentation

- hardware: document ML307R LED behavior
```

The release script groups commits by type:

- `feat` -> `Features`
- `fix` -> `Bug Fixes`
- `perf` -> `Performance`
- `docs` -> `Documentation`
- `test` -> `Tests`
- `build` -> `Build`
- `refactor` -> `Refactoring`
- `chore` -> `Chores`

## Local Release Script

The release flow is driven by:

```bash
python3 scripts/release.py
```

Expected commands:

```bash
python3 scripts/release.py --dry-run
python3 scripts/release.py --bump auto
python3 scripts/release.py --bump patch
python3 scripts/release.py --bump minor
python3 scripts/release.py --bump major
python3 scripts/release.py --bump auto --push
python3 scripts/release.py --bump auto --push --github-release
```

Behavior:

- `--dry-run` prints the next version and changelog preview without changing files.
- `--bump auto` chooses the next version from commit messages.
- `--bump patch|minor|major` forces the release type.
- `--push` pushes `main` and the release tag to `origin`.
- `--github-release` creates a GitHub Release page with the generated notes and
  firmware asset. It requires `--push`.

The script must refuse to release if:

- The current branch is not `main`.
- The working tree is not clean.
- Native tests fail.
- Firmware build fails.
- The release tag already exists.

Before creating a release, the script runs:

```bash
pio test -e native
pio run
```

Release tool logic is covered by Python standard-library unit tests:

```bash
PYTHONDONTWRITEBYTECODE=1 python3 scripts/test_release.py
```

A successful release creates:

- Updated `VERSION`.
- Updated `CHANGELOG.md`.
- A release commit:

  ```text
  chore(release): v0.1.0
  ```

- An annotated Git tag:

  ```bash
  git tag -a v0.1.0 -m "Release v0.1.0"
  ```

## Recommended Release Workflow

1. Make sure `main` is up to date:

   ```bash
   git switch main
   git pull
   ```

2. Preview the release:

   ```bash
   python3 scripts/release.py --dry-run
   ```

3. Create the local release:

   ```bash
   python3 scripts/release.py --bump auto
   ```

4. Inspect the result:

   ```bash
   git log --oneline --decorate -5
   git status --short --branch
   ```

5. Push the release:

   ```bash
   git push origin main
   git push origin v0.1.0
   ```

   Or use:

   ```bash
   python3 scripts/release.py --bump auto --push
   ```

   To also publish the GitHub Release page and upload the firmware binary:

   ```bash
   python3 scripts/release.py --bump auto --push --github-release
   ```

## Hardware Release Notes

A release tag means the code built successfully and native tests passed.

For stable firmware releases, also perform hardware validation:

- Upload firmware to ESP32-C3.
- Confirm USB serial logs appear.
- Confirm the modem responds to `AT`.
- Confirm `AT+CMGF=0` succeeds.
- Confirm `AT+CNMI=2,2,0,0,0` succeeds.
- Send a real SMS and confirm it is decoded.
- For stable releases, run a 24-72 hour soak test.

Record hardware validation results manually in the release notes when needed.

## GitHub Release Page

GitHub Release publishing is optional and requires GitHub CLI:

```bash
gh auth login
```

Create the release commit, push `main`, push the tag, create the GitHub Release
page, and upload the firmware binary with:

```bash
python3 scripts/release.py --bump auto --push --github-release
```

The GitHub Release notes are generated from the same changelog entry inserted
into `CHANGELOG.md`. The script does not upload `CHANGELOG.md` as a separate
asset.

The uploaded firmware asset is named with the release version, for example:

```text
firmware-v0.1.0.bin
```

Internally the script uses `gh release create --verify-tag` so GitHub CLI will
fail instead of creating a release from a tag that has not been pushed.

## Notes for AI Agents

- Do not create release commits from feature branches.
- Do not edit old changelog entries unless correcting a clear mistake.
- Do not rewrite existing release tags.
- Do not release with failing tests or failing firmware build.
- Keep commit messages in Conventional Commit format.
- Prefer `0.x.y` versions until the SMS receive, queue, forwarding, web status,
  and recovery paths are stable.
