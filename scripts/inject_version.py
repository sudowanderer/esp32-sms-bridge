from pathlib import Path

Import("env")

project_dir = Path(env["PROJECT_DIR"])
version_path = project_dir / "VERSION"
version = version_path.read_text(encoding="utf-8").strip()

env.Append(CPPDEFINES=[("APP_VERSION", f'\\"{version}\\"')])
