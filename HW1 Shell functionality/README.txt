# Smash – Mini Unix Shell  
**Operating Systems (234123) – Wet HW 1, Spring 2025, Technion**

> Personal implementation of a limited Unix-like shell, developed as part of the Technion Operating Systems course.  
> 📄 Full assignment spec: see `WHW1.pdf` in this repository.

---

## ✨ Features

| Category | Details |
|----------|---------|
| **Built-in commands** | `chprompt`, `showpid`, `pwd`, `cd`, `jobs`, `fg`, `quit`, `kill`, `alias`, `unalias`, `unsetenv`, `watchproc` |
| **External commands** | Regular executables via `execvp`; patterns containing `*` or `?` are delegated to `/bin/bash -c` |
| **Background jobs** | Trailing `&` launches the job in the background and tracks it in a **Jobs List** |
| **I/O redirection** | `>` (overwrite) and `>>` (append) |
| **Pipes** | `cmd1 \| cmd2` and `cmd1 \|& cmd2` (stdout or stderr) |
| **Signal handling** | *Ctrl-C* (`SIGINT`) cleanly terminates the current foreground job |
| **Resource monitor** | `watchproc <pid>` – one-shot snapshot of CPU % and RAM usage |
| **Limits (per spec)** | ≤ 100 concurrent jobs · command line ≤ 200 chars · ≤ 20 args each |

---
## ⚙️ Build

```bash
git clone https://github.com/yonadavkakun/OS_HW1.git
cd OS_HW1
make 

./smash
smash> showpid
smash pid is 4242

smash> sleep 100 &
smash> jobs
[1] sleep 100 &

smash> fg 1
sleep 100 &
^Csmash: got ctrl-C
smash: process 4243 was killed
