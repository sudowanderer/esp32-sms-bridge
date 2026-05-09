#!/usr/bin/env python3
"""Local release helper for the ESP32 SMS bridge firmware.

The script intentionally has no third-party Python dependencies. It reads git
history, applies a small Conventional Commits parser, updates VERSION and
CHANGELOG.md, creates a release commit, and creates an annotated git tag.
"""

from __future__ import annotations

import argparse
import dataclasses
import datetime as _dt
import re
import subprocess
import sys
import tempfile
from collections import OrderedDict
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
VERSION_FILE = ROOT / "VERSION"
CHANGELOG_FILE = ROOT / "CHANGELOG.md"
CHANGELOG_MARKER = "<!-- releases -->"
TAG_PATTERN = "v[0-9]*"
FIRMWARE_BIN = ROOT / ".pio" / "build" / "esp32-c3-devkitm-1" / "firmware.bin"
COMMIT_RE = re.compile(
    r"^(?P<type>[a-z]+)(?:\((?P<scope>[^)]+)\))?(?P<breaking>!)?: (?P<summary>.+)$"
)
SEMVER_RE = re.compile(r"^(0|[1-9]\d*)\.(0|[1-9]\d*)\.(0|[1-9]\d*)$")

TYPE_SECTIONS = OrderedDict(
    [
        ("feat", "Features"),
        ("fix", "Bug Fixes"),
        ("perf", "Performance"),
        ("docs", "Documentation"),
        ("test", "Tests"),
        ("build", "Build"),
        ("refactor", "Refactoring"),
        ("chore", "Chores"),
    ]
)


@dataclasses.dataclass(frozen=True)
class Version:
    major: int
    minor: int
    patch: int

    @classmethod
    def parse(cls, raw: str) -> "Version":
        value = raw.strip()
        match = SEMVER_RE.match(value)
        if not match:
            raise ReleaseError(f"invalid SemVer value: {value!r}")
        return cls(*(int(part) for part in match.groups()))

    def bump(self, release_type: str) -> "Version":
        if release_type == "major":
            return Version(self.major + 1, 0, 0)
        if release_type == "minor":
            return Version(self.major, self.minor + 1, 0)
        if release_type == "patch":
            return Version(self.major, self.minor, self.patch + 1)
        raise ReleaseError(f"unknown bump type: {release_type}")

    def __str__(self) -> str:
        return f"{self.major}.{self.minor}.{self.patch}"


@dataclasses.dataclass
class Commit:
    sha: str
    subject: str
    body: str
    type: str | None = None
    scope: str | None = None
    summary: str | None = None
    breaking: bool = False

    @classmethod
    def parse(cls, sha: str, subject: str, body: str) -> "Commit":
        match = COMMIT_RE.match(subject)
        if not match:
            return cls(sha=sha, subject=subject, body=body)

        commit_type = match.group("type")
        breaking = bool(match.group("breaking")) or "BREAKING CHANGE:" in body
        return cls(
            sha=sha,
            subject=subject,
            body=body,
            type=commit_type,
            scope=match.group("scope"),
            summary=match.group("summary"),
            breaking=breaking,
        )


class ReleaseError(Exception):
    pass


def run(args: list[str], *, check: bool = True, strip: bool = True) -> str:
    result = subprocess.run(
        args,
        cwd=ROOT,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        check=False,
    )
    if check and result.returncode != 0:
        message = result.stderr.strip() or result.stdout.strip()
        raise ReleaseError(f"{' '.join(args)} failed: {message}")
    return result.stdout.strip() if strip else result.stdout


def run_stream(args: list[str]) -> None:
    result = subprocess.run(args, cwd=ROOT, check=False)
    if result.returncode != 0:
        raise ReleaseError(f"{' '.join(args)} failed with exit code {result.returncode}")


def git(args: list[str], *, check: bool = True) -> str:
    return run(["git", *args], check=check)


def current_branch() -> str:
    return git(["rev-parse", "--abbrev-ref", "HEAD"])


def ensure_release_ready() -> None:
    branch = current_branch()
    if branch != "main":
        raise ReleaseError(f"release must run from main, current branch is {branch!r}")

    status = git(["status", "--porcelain"])
    if status:
        raise ReleaseError("working tree is not clean")

    git(["remote", "get-url", "origin"])


def latest_release_tag() -> str | None:
    tag = git(["describe", "--tags", "--abbrev=0", "--match", TAG_PATTERN], check=False)
    return tag or None


def read_current_version(tag: str | None) -> Version:
    if VERSION_FILE.exists():
        return Version.parse(VERSION_FILE.read_text(encoding="utf-8"))

    if tag is not None:
        return Version.parse(tag.removeprefix("v"))

    return Version(0, 0, 0)


def tag_exists(tag: str) -> bool:
    result = git(["rev-parse", "-q", "--verify", f"refs/tags/{tag}"], check=False)
    return bool(result)


def read_commits(since_tag: str | None) -> list[Commit]:
    revision_range = f"{since_tag}..HEAD" if since_tag else "HEAD"
    raw = run(["git", "log", "--pretty=format:%H%x1f%s%x1f%b%x1e", revision_range], strip=False)
    return parse_git_log(raw)


def parse_git_log(raw: str) -> list[Commit]:
    commits: list[Commit] = []

    for record in raw.split("\x1e"):
        record = record.strip("\n")
        if not record:
            continue
        parts = record.split("\x1f", 2)
        if len(parts) != 3:
            continue
        commits.append(Commit.parse(parts[0], parts[1], parts[2]))

    commits.reverse()
    return commits


def recommended_bump(commits: list[Commit], current: Version, has_tag: bool) -> str | None:
    if not has_tag and current == Version(0, 0, 0):
        return "minor"

    has_breaking = any(commit.breaking for commit in commits)
    if has_breaking:
        return "minor" if current.major == 0 else "major"

    if any(commit.type == "feat" for commit in commits):
        return "minor"

    if any(commit.type in {"fix", "perf"} for commit in commits):
        return "patch"

    return None


def group_commits(commits: list[Commit]) -> OrderedDict[str, list[str]]:
    groups: OrderedDict[str, list[str]] = OrderedDict((section, []) for section in TYPE_SECTIONS.values())
    groups["Other Changes"] = []

    for commit in commits:
        if commit.type in TYPE_SECTIONS and commit.summary:
            prefix = f"{commit.scope}: " if commit.scope else ""
            breaking = " [breaking]" if commit.breaking else ""
            groups[TYPE_SECTIONS[commit.type]].append(
                f"- {prefix}{commit.summary}{breaking} ({commit.sha[:7]})"
            )
            continue

        groups["Other Changes"].append(f"- {commit.subject} ({commit.sha[:7]})")

    return OrderedDict((section, lines) for section, lines in groups.items() if lines)


def render_changelog_entry(version: Version, commits: list[Commit]) -> str:
    today = _dt.date.today().isoformat()
    groups = group_commits(commits)
    lines = [f"## [{version}] - {today}", ""]

    if not groups:
        lines.extend(["### Chores", "", "- release maintenance"])
    else:
        for section, items in groups.items():
            lines.extend([f"### {section}", "", *items, ""])

    return "\n".join(lines).rstrip() + "\n"


def update_changelog(entry: str) -> None:
    if CHANGELOG_FILE.exists():
        existing = CHANGELOG_FILE.read_text(encoding="utf-8").rstrip() + "\n"
    else:
        existing = f"# Changelog\n\n{CHANGELOG_MARKER}\n"

    if CHANGELOG_MARKER in existing:
        new_content = existing.replace(CHANGELOG_MARKER, f"{CHANGELOG_MARKER}\n\n{entry}", 1)
        CHANGELOG_FILE.write_text(new_content, encoding="utf-8")
        return

    lines = existing.splitlines()
    if not lines:
        new_content = "# Changelog\n\n" + entry
    elif lines[0].strip() == "# Changelog":
        rest = "\n".join(lines[1:]).strip()
        new_content = "# Changelog\n\n" + entry
        if rest:
            new_content += "\n" + rest + "\n"
    else:
        new_content = "# Changelog\n\n" + entry + "\n" + existing

    CHANGELOG_FILE.write_text(new_content, encoding="utf-8")


def write_version(version: Version) -> None:
    VERSION_FILE.write_text(f"{version}\n", encoding="utf-8")


def print_preview(current: Version, next_version: Version, bump: str, tag: str, entry: str) -> None:
    print(f"current_version={current}")
    print(f"next_version={next_version}")
    print(f"bump={bump}")
    print(f"tag={tag}")
    print()
    print(entry.rstrip())


def create_release(next_version: Version, entry: str, *, push: bool) -> None:
    tag = f"v{next_version}"
    write_version(next_version)
    update_changelog(entry)

    git(["add", "VERSION", "CHANGELOG.md"])
    git(["commit", "-m", f"chore(release): {tag}"])
    git(["tag", "-a", tag, "-m", f"Release {tag}"])

    if push:
        git(["push", "origin", "main"])
        git(["push", "origin", tag])


def build_github_release_command(tag: str, firmware_path: Path, notes_path: Path) -> list[str]:
    version = tag.removeprefix("v")
    asset = f"{firmware_path}#firmware-v{version}.bin"
    return [
        "gh",
        "release",
        "create",
        tag,
        asset,
        "--title",
        tag,
        "--notes-file",
        str(notes_path),
        "--verify-tag",
    ]


def create_github_release(tag: str, entry: str, *, firmware_path: Path = FIRMWARE_BIN) -> None:
    if not firmware_path.exists():
        raise ReleaseError(f"firmware asset not found: {firmware_path}")

    with tempfile.NamedTemporaryFile("w", encoding="utf-8", suffix=".md") as notes_file:
        notes_file.write(entry)
        notes_file.flush()
        run_stream(build_github_release_command(tag, firmware_path, Path(notes_file.name)))


def parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Create a local SemVer firmware release.")
    parser.add_argument(
        "--bump",
        choices=["auto", "patch", "minor", "major"],
        default="auto",
        help="Version bump to apply. Default: auto.",
    )
    parser.add_argument(
        "--dry-run",
        action="store_true",
        help="Print the release preview without changing files, committing, or tagging.",
    )
    parser.add_argument(
        "--push",
        action="store_true",
        help="Push main and the release tag after creating the release.",
    )
    parser.add_argument(
        "--github-release",
        action="store_true",
        help="Create a GitHub Release with the generated notes and firmware asset. Requires --push.",
    )
    return parser.parse_args(argv)


def validate_args(args: argparse.Namespace) -> None:
    if args.github_release and not args.push:
        raise ReleaseError("--github-release requires --push")


def main(argv: list[str]) -> int:
    args = parse_args(argv)

    try:
        validate_args(args)
        ensure_release_ready()

        tag = latest_release_tag()
        current = read_current_version(tag)
        commits = read_commits(tag)
        if not commits:
            raise ReleaseError("no commits found for release")

        bump = recommended_bump(commits, current, has_tag=tag is not None) if args.bump == "auto" else args.bump
        if bump is None:
            raise ReleaseError("no release-worthy commits found; use --bump patch|minor|major to force")

        next_version = current.bump(bump)
        next_tag = f"v{next_version}"
        if tag_exists(next_tag):
            raise ReleaseError(f"tag already exists: {next_tag}")

        entry = render_changelog_entry(next_version, commits)
        print_preview(current, next_version, bump, next_tag, entry)

        if args.dry_run:
            return 0

        print("running native tests...")
        run_stream(["pio", "test", "-e", "native"])
        print("building firmware...")
        run_stream(["pio", "run"])

        create_release(next_version, entry, push=args.push)
        print(f"created release {next_tag}")
        if args.push:
            print(f"pushed main and {next_tag}")
        if args.github_release:
            print(f"creating GitHub Release {next_tag}...")
            create_github_release(next_tag, entry)
            print(f"created GitHub Release {next_tag}")
        return 0
    except ReleaseError as exc:
        print(f"release error: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
