# git_version.py
from SCons.Script import Import
import subprocess

Import("env")

git_version = subprocess.check_output(
    ["git", "rev-parse", "--short", "HEAD"],
    text=True
).strip()

env.Append(
    CPPDEFINES=[
        ("GIT_VERSION", '\\"{}\\"'.format(git_version))
    ]
)

