# Posts review comments with suggestion blocks for each diff hunk.
#
# Requires environment variables (set by the workflow):
#   GITHUB_REPOSITORY  - e.g. "CleverRaven/Cataclysm-DDA"
#   GH_TOKEN           - GitHub token for API access

import os
import re
import subprocess
import sys

from github import Commit, Github, GithubException, PullRequest

def read_file(path: str):
    with open(path, "rb") as f:
        return f.read()

def main():
    token = os.environ.get("GH_TOKEN", "")
    repo_name = os.environ.get("GITHUB_REPOSITORY", "")

    pr_number = read_file("pr_number.txt")
    commit_sha = read_file("commit_sha.txt")
    diff_text = read_file("diff.txt")
    comment = read_file("comment.txt")

    if not all([token, repo_name, pr_number, commit_sha, comment]):
        print("Missing required inputs -- skipping comment posting.",
              file=sys.stderr)
        return

    gh = Github(token)
    repo = gh.get_repo(repo_name)
    pr = repo.get_pull(int(pr_number))
    commit = repo.get_commit(commit_sha)

    comment_marker_tag = base64.b64encode(comment.encode("utf-8")).decode("utf-8")
    comment_marker = "<!-- {}-suggest -->".format(comment_marker_tag)

    print("Diff text:")
    print(diff_text)
    file_hunks = parse_hunks(diff_text)
    print("Files with changes: %s" % ", ".join(file_hunks.keys()))

    post_suggestions(
        file_hunks,
        comment_marker,
        comment,
        pr,
        commit,
    )
    return


def parse_hunks(diff_text: str) -> dict[str, list[dict]]:
    """Parse a unified diff into per-file lists of hunks.

    Each hunk has old_start, old_count (the original line range)
    and new_lines (the replacement content for a suggestion block).
    """
    files = {}
    current_file = None
    in_hunk = False

    for line in diff_text.split("\n"):
        m = re.match(r"^diff --git a/.+ b/(.+)$", line)
        if m:
            current_file = m.group(1)
            files.setdefault(current_file, [])
            in_hunk = False
            continue

        m = re.match(r"^@@ -(\d+)(?:,(\d+))? \+(\d+)(?:,(\d+))? @@", line)
        if m and current_file is not None:
            old_start = int(m.group(1))
            old_count = int(m.group(2)) if m.group(2) is not None else 1
            files[current_file].append({
                "old_start": old_start,
                "old_count": old_count,
                "new_lines": [],
            })
            in_hunk = True
            continue

        if in_hunk and current_file is not None and files[current_file]:
            hunk = files[current_file][-1]
            if line.startswith("+"):
                hunk["new_lines"].append(line[1:])
            elif line.startswith(" "):
                hunk["new_lines"].append(line[1:])
            # '-' lines are already counted in old_count;
            # '\' (no newline) lines are ignored.

    return files


def post_suggestions(
        file_hunks: dict[str, list[dict]],
        comment_marker: str,
        comment_text: str,
        pr: PullRequest,
        commit: Commit,
):
    """Post review comments with suggestion blocks for each hunk."""
    print("::group::Posting suggestions")

    # Delete stale comments from previous runs.
    for comment in pr.get_review_comments():
        if comment_marker in comment.body:
            print("Deleting stale comment %d on %s"
                  % (comment.id, comment.path))
            comment.delete()

    # Post one suggestion per hunk.
    for path, hunks in file_hunks.items():
        for hunk in hunks:
            old_start = hunk["old_start"]
            old_count = hunk["old_count"]
            new_content = "\n".join(hunk["new_lines"])

            if old_count < 1:
                continue

            body = "%s\n%s\n```suggestion\n%s\n```" % (
                comment_marker, comment_text, new_content)
            end_line = old_start + old_count - 1

            try:
                print("Posting suggestion on %s lines %d-%d"
                      % (path, old_start, end_line))
                if old_count > 1:
                    pr.create_review_comment(
                        body=body, commit=commit, path=path,
                        line=end_line, start_line=old_start,
                        side="RIGHT")
                else:
                    pr.create_review_comment(
                        body=body, commit=commit, path=path,
                        line=old_start, side="RIGHT")
            except GithubException as e:
                print("Failed to post on %s:%d - %s"
                      % (path, old_start, e), file=sys.stderr)

    print("::endgroup::")


if __name__ == "__main__":
    main()
