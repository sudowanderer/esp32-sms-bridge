#!/usr/bin/env python3
"""Unit tests for scripts/release.py pure release logic."""

from __future__ import annotations

import argparse
import tempfile
import unittest
from pathlib import Path
from unittest import mock

import release


class VersionTest(unittest.TestCase):
    def test_parse_valid_version(self) -> None:
        self.assertEqual(release.Version.parse("0.1.2"), release.Version(0, 1, 2))

    def test_parse_rejects_invalid_version(self) -> None:
        with self.assertRaises(release.ReleaseError):
            release.Version.parse("01.2.3")

    def test_bump_patch(self) -> None:
        self.assertEqual(release.Version(0, 1, 2).bump("patch"), release.Version(0, 1, 3))

    def test_bump_minor_resets_patch(self) -> None:
        self.assertEqual(release.Version(0, 1, 2).bump("minor"), release.Version(0, 2, 0))

    def test_bump_major_resets_minor_and_patch(self) -> None:
        self.assertEqual(release.Version(0, 1, 2).bump("major"), release.Version(1, 0, 0))


class CommitParserTest(unittest.TestCase):
    def test_parse_conventional_commit(self) -> None:
        commit = release.Commit.parse("abcdef1", "feat(sms): decode incoming PDU messages", "")

        self.assertEqual(commit.type, "feat")
        self.assertEqual(commit.scope, "sms")
        self.assertEqual(commit.summary, "decode incoming PDU messages")
        self.assertFalse(commit.breaking)

    def test_parse_breaking_bang(self) -> None:
        commit = release.Commit.parse("abcdef1", "feat(config)!: change stored config format", "")

        self.assertTrue(commit.breaking)
        self.assertEqual(commit.type, "feat")
        self.assertEqual(commit.scope, "config")

    def test_parse_breaking_footer(self) -> None:
        commit = release.Commit.parse(
            "abcdef1",
            "fix(config): load defaults",
            "BREAKING CHANGE: existing NVS config must be reset",
        )

        self.assertTrue(commit.breaking)

    def test_non_conventional_commit_is_kept_as_other_change(self) -> None:
        commit = release.Commit.parse("abcdef1", "Initial project scaffold", "")

        self.assertIsNone(commit.type)
        self.assertEqual(commit.subject, "Initial project scaffold")

    def test_parse_git_log_keeps_empty_body_field_separator(self) -> None:
        raw = "abcdef1\x1ffeat(sms): decode PDU\x1f\x1e\n1234567\x1ffix: handle timeout\x1fbody\x1e"

        commits = release.parse_git_log(raw)

        self.assertEqual([commit.subject for commit in commits], ["fix: handle timeout", "feat(sms): decode PDU"])
        self.assertEqual(commits[1].type, "feat")
        self.assertEqual(commits[1].scope, "sms")
        self.assertEqual(commits[1].body, "")


class RecommendedBumpTest(unittest.TestCase):
    def test_first_release_from_zero_defaults_to_minor(self) -> None:
        commits = [release.Commit.parse("abcdef1", "docs: add release notes", "")]

        bump = release.recommended_bump(commits, release.Version(0, 0, 0), has_tag=False)

        self.assertEqual(bump, "minor")

    def test_feature_recommends_minor(self) -> None:
        commits = [release.Commit.parse("abcdef1", "feat(sms): decode PDU", "")]

        bump = release.recommended_bump(commits, release.Version(0, 1, 0), has_tag=True)

        self.assertEqual(bump, "minor")

    def test_fix_recommends_patch(self) -> None:
        commits = [release.Commit.parse("abcdef1", "fix(modem): handle timeout", "")]

        bump = release.recommended_bump(commits, release.Version(0, 1, 0), has_tag=True)

        self.assertEqual(bump, "patch")

    def test_breaking_change_in_zero_major_recommends_minor(self) -> None:
        commits = [release.Commit.parse("abcdef1", "feat(config)!: change format", "")]

        bump = release.recommended_bump(commits, release.Version(0, 1, 0), has_tag=True)

        self.assertEqual(bump, "minor")

    def test_breaking_change_after_one_major_recommends_major(self) -> None:
        commits = [release.Commit.parse("abcdef1", "feat(config)!: change format", "")]

        bump = release.recommended_bump(commits, release.Version(1, 2, 3), has_tag=True)

        self.assertEqual(bump, "major")

    def test_docs_only_after_existing_tag_has_no_recommended_bump(self) -> None:
        commits = [release.Commit.parse("abcdef1", "docs: explain release process", "")]

        bump = release.recommended_bump(commits, release.Version(0, 1, 0), has_tag=True)

        self.assertIsNone(bump)


class ChangelogTest(unittest.TestCase):
    def test_group_commits_by_type(self) -> None:
        commits = [
            release.Commit.parse("1111111", "feat(sms): decode PDU", ""),
            release.Commit.parse("2222222", "fix(modem): handle timeout", ""),
            release.Commit.parse("3333333", "docs: explain release process", ""),
        ]

        groups = release.group_commits(commits)

        self.assertEqual(groups["Features"], ["- sms: decode PDU (1111111)"])
        self.assertEqual(groups["Bug Fixes"], ["- modem: handle timeout (2222222)"])
        self.assertEqual(groups["Documentation"], ["- explain release process (3333333)"])

    def test_render_changelog_entry_contains_sections_and_commits(self) -> None:
        commits = [
            release.Commit.parse("1111111", "feat(sms): decode PDU", ""),
            release.Commit.parse("2222222", "fix(modem): handle timeout", ""),
        ]

        entry = release.render_changelog_entry(release.Version(0, 1, 0), commits)

        self.assertIn("## [0.1.0] - ", entry)
        self.assertIn("### Features", entry)
        self.assertIn("- sms: decode PDU (1111111)", entry)
        self.assertIn("### Bug Fixes", entry)
        self.assertIn("- modem: handle timeout (2222222)", entry)


class GithubReleaseTest(unittest.TestCase):
    def test_github_release_requires_push(self) -> None:
        args = argparse.Namespace(github_release=True, push=False)

        with self.assertRaises(release.ReleaseError):
            release.validate_args(args)

    def test_github_release_is_allowed_with_push(self) -> None:
        args = argparse.Namespace(github_release=True, push=True)

        release.validate_args(args)

    def test_build_github_release_command_uses_notes_and_firmware_asset(self) -> None:
        command = release.build_github_release_command(
            "v0.1.0",
            Path(".pio/build/esp32-c3-devkitm-1/firmware.bin"),
            Path("/tmp/release-notes.md"),
        )

        self.assertEqual(command[0:4], ["gh", "release", "create", "v0.1.0"])
        self.assertIn(".pio/build/esp32-c3-devkitm-1/firmware.bin#firmware-v0.1.0.bin", command)
        self.assertIn("--title", command)
        self.assertIn("v0.1.0", command)
        self.assertIn("--notes-file", command)
        self.assertIn("/tmp/release-notes.md", command)
        self.assertIn("--verify-tag", command)

    def test_create_github_release_writes_changelog_entry_to_notes_file(self) -> None:
        with tempfile.TemporaryDirectory() as tmp_dir:
            firmware_path = Path(tmp_dir) / "firmware.bin"
            firmware_path.write_bytes(b"firmware")
            entry = release.render_changelog_entry(
                release.Version(0, 1, 0),
                [release.Commit.parse("1111111", "feat(sms): decode PDU", "")],
            )
            captured = {}

            def fake_run_stream(command: list[str]) -> None:
                notes_index = command.index("--notes-file") + 1
                captured["command"] = command
                captured["notes"] = Path(command[notes_index]).read_text(encoding="utf-8")

            with mock.patch.object(release, "run_stream", side_effect=fake_run_stream):
                release.create_github_release("v0.1.0", entry, firmware_path=firmware_path)

            self.assertEqual(captured["notes"], entry)
            self.assertIn(f"{firmware_path}#firmware-v0.1.0.bin", captured["command"])


if __name__ == "__main__":
    unittest.main()
