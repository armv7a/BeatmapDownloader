import subprocess
import time

version_file = "./src/dlver.h"

VERSION_STR = "2.7"
VERSION = 27

COMMIT_LENGTH = 7


def get_commit_id() -> str:
    try:
        commit = subprocess.run(
            ["git", "rev-parse", f"--short={COMMIT_LENGTH}", "HEAD"],
            capture_output=True,
            text=True,
            check=True,
        ).stdout.strip()
        if not commit:
            return "unknown"

        dirty = subprocess.run(
            ["git", "status", "--porcelain"],
            capture_output=True,
            text=True,
            check=True,
        ).stdout.strip()
        return commit + "-dirty" if dirty else commit
    except Exception as e:
        print("get commit id failed:", e)
        return "unknown"


def update_version_file(commit: str):
    with open(version_file, "w") as f:
        f.write("#pragma once\n")
        f.write("\n")
        f.write("#define LATEST_UPDATE_TIMESTAMP " + str(int(time.time())) + "\n")
        f.write("#define DOWNLOADER_VERSION " + str(VERSION) + "\n")
        f.write('#define DOWNLOADER_VERSION_STR "' + VERSION_STR + '"\n')
        f.write('#define DOWNLOADER_COMMIT "' + commit + '"\n')


def main():
    commit = get_commit_id()
    print(f"current version: {VERSION_STR}({VERSION}), commit: {commit}")

    update_version_file(commit)


if __name__ == "__main__":
    main()
