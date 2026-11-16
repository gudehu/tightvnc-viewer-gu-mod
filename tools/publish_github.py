# -*- coding: utf-8 -*-
"""
Publish tightvnc-gu-mod to GitHub (create repo + push source + release exe).

This script does NOT contain any credentials. Provide authentication in one
of these ways:

  Option A - GitHub CLI (recommended):
      Install gh, run `gh auth login`, then just run:
          python tools/publish_github.py

  Option B - Personal Access Token (PAT):
      Create a PAT at https://github.com/settings/tokens with `repo` scope,
      then set environment variable GITHUB_TOKEN and GITHUB_USER:
          set GITHUB_USER=your-username
          set GITHUB_TOKEN=ghp_xxxxxxxxxxxx
          python tools/publish_github.py

By default it creates a PUBLIC repository named "tightvnc-gu-mod".
To use a different name/owner, edit REPO_NAME / OWNER below or pass env vars
GH_REPO and GH_OWNER.
"""
import os
import subprocess
import sys
import json

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)

REPO_NAME = os.environ.get("GH_REPO", "tightvnc-gu-mod")
OWNER = os.environ.get("GH_OWNER", "").strip()  # optional; defaults to authed user
DESCRIPTION = "Modified TightVNC 2.8.90 Viewer (GPL v2): two-way file transfer, one-click toolbar tools, IME fix."


def run(cmd, **kw):
    print("+", " ".join(str(c) for c in cmd))
    return subprocess.run(cmd, check=True, **kw)


def has_gh():
    try:
        subprocess.run(["gh", "--version"], capture_output=True, check=True)
        return True
    except Exception:
        return False


def publish_with_gh():
    repo_arg = REPO_NAME
    if OWNER:
        repo_arg = f"{OWNER}/{REPO_NAME}"
    # Create public repo (idempotent-ish: ignore error if exists)
    r = subprocess.run(
        ["gh", "repo", "create", repo_arg, "--public",
         "--description", DESCRIPTION, "--source", ROOT, "--push"],
        cwd=ROOT, capture_output=True, text=True,
    )
    if r.returncode != 0:
        print("repo create note:", (r.stderr or r.stdout).strip())
        print("If the repo already exists, pushing into the existing one instead.")
        run(["git", "-C", ROOT, "init"], check=False)
        url = f"https://github.com/{repo_arg}.git"
        # ensure remote
        rr = subprocess.run(["git", "-C", ROOT, "remote", "get-url", "origin"],
                            capture_output=True, text=True)
        if rr.returncode != 0:
            run(["git", "-C", ROOT, "remote", "add", "origin", url])
        else:
            run(["git", "-C", ROOT, "remote", "set-url", "origin", url])
    run(["git", "-C", ROOT, "add", "-A"])
    rr = subprocess.run(["git", "-C", ROOT, "status", "--porcelain"],
                        capture_output=True, text=True)
    if rr.stdout.strip():
        run(["git", "-C", ROOT, "commit", "-m", "Initial release of tightvnc-gu mod"])
    run(["git", "-C", ROOT, "branch", "-M", "main"])
    run(["git", "-C", ROOT, "push", "-u", "origin", "main"])
    print("Pushed. Repo:", f"https://github.com/{repo_arg}")


def publish_with_token():
    user = OWNER or os.environ.get("GITHUB_USER", "")
    token = os.environ.get("GITHUB_TOKEN", "")
    if not user or not token:
        print("GITHUB_USER and GITHUB_TOKEN must be set for token mode.")
        return 1
    import urllib.request, urllib.error
    api = "https://api.github.com"

    def api_call(method, path, data=None):
        body = json.dumps(data).encode() if data is not None else None
        req = urllib.request.Request(api + path, data=body, method=method)
        req.add_header("Authorization", f"Bearer {token}")
        req.add_header("Accept", "application/vnd.github+json")
        req.add_header("User-Agent", "tightvnc-gu-mod-publisher")
        if body:
            req.add_header("Content-Type", "application/json")
        try:
            with urllib.request.urlopen(req, timeout=60) as resp:
                return resp.status, json.loads(resp.read().decode() or "{}")
        except urllib.error.HTTPError as e:
            return e.code, json.loads(e.read().decode() or "{}")

    status, info = api_call("POST", "/user/repos", {
        "name": REPO_NAME, "description": DESCRIPTION,
        "public": True, "auto_init": False,
    })
    if status not in (201, 422):
        print("create repo failed:", status, info)
        return 1
    if status == 422:
        print("repo may already exist; continuing to push into it.")
    full = info.get("full_name") or f"{user}/{REPO_NAME}"
    clone_url = info.get("clone_url") or f"https://{user}:{token}@github.com/{full}.git"
    if token and clone_url.startswith("https://github.com"):
        clone_url = clone_url.replace("https://", f"https://{token}@")

    run(["git", "-C", ROOT, "init"], check=False)
    rr = subprocess.run(["git", "-C", ROOT, "remote", "get-url", "origin"],
                        capture_output=True, text=True)
    if rr.returncode != 0:
        run(["git", "-C", ROOT, "remote", "add", "origin", clone_url])
    else:
        run(["git", "-C", ROOT, "remote", "set-url", "origin", clone_url])
    run(["git", "-C", ROOT, "add", "-A"])
    rr = subprocess.run(["git", "-C", ROOT, "status", "--porcelain"],
                        capture_output=True, text=True)
    if rr.stdout.strip():
        run(["git", "-C", ROOT, "commit", "-m", "Initial release of tightvnc-gu mod"])
    run(["git", "-C", ROOT, "branch", "-M", "main"])
    run(["git", "-C", ROOT, "push", "-u", "origin", "main"])
    print("Pushed. Repo:", f"https://github.com/{full}")
    return 0


def main():
    if not os.path.isdir(os.path.join(ROOT, "src")):
        print("Release directory looks wrong (no src/).")
        return 1
    if has_gh():
        print("Using GitHub CLI (gh) for authentication.")
        publish_with_gh()
    else:
        print("gh not found; using token mode.")
        return publish_with_token() or 0
    return 0


if __name__ == "__main__":
    sys.exit(main())
