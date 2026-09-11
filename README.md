# Chronicle 📜

**The human-friendly version control system built for developers who hate memorizing Git commands.**

Chronicle eliminates Git’s steep learning curve with plain-English commands, automatic file tracking, and built-in safety nets, all while maintaining 100% native compatibility with standard .git repositories, GitHub, GitLab and Codeberg.

---

## Key Features

* **No Staging Area:** Never run git add again. Modified and new files are automatically included in your saves unless listed in .gitignore.
* **Human-Readable Terminology:** Ditch obscure terms like detached HEAD, staging index, and cherry-pick.
* **Native Git Storage:** Built on libgit2. Teammates using standard Git won't even know you're using Chronicle.
* **Seamless Authentication:** Automatically hooks into Windows Credential Manager and SSH keys to push changes to Codeberg/GitHub smoothly.

---

## Command Reference

| Command | What it does | Git Equivalent |
| :--- | :--- | :--- |
| `chronicle init` | Initializes a new repository | `git init` |
| `chronicle save "msg"` | Saves a snapshot of all current changes | `git add -A && git commit -m "msg"` |
| `chronicle history` | Displays a visual timeline of previous checkpoints | `git log --oneline` |
| `chronicle status` | Lists all unsaved draft changes | `git status` |
| `chronicle diff` | Shows line-by-line edits in your draft changes | `git diff` |
| `chronicle discard` | Wipes unsaved local edits and restores files | `git reset --hard HEAD` |
| `chronicle timeline <name>` | Creates or switches to a parallel track of work | `git checkout -b <name>` |
| `chronicle sync [remote]` | Pushes active timeline changes to a remote server | `git push origin <branch>` |
| `chronicle undo` | Reverts the repository to the previous state | `git reset --hard HEAD@{1}` |

---

## Building from Source (Windows MSVC)

### Prerequisites

1. **Visual Studio 2022** with the **Desktop development with C++** workload installed.
2. **vcpkg** with the static 64-bit `libgit2` package:
   ```powershell
   vcpkg install libgit2:x64-windows-static
