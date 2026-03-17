# minishell-c

English | [中文版](README.md)

A command-line shell implemented in C.

Notes:
- The source repository contains the full codebase and build files.
- Prebuilt release packages include the executable and usage docs only (no source tree).

This document is intended for first-time users and focuses on features and usage.

## Features

- Interactive input (backspace and up/down history navigation)
- Builtins: echo, type, exit, pwd, cd, history
- External command execution via PATH lookup
- Argument parsing (quotes and escaping)
- Redirection (stdout/stderr, overwrite and append)
- Pipeline execution
- TAB completion (commands and file paths)
- Persistent history (`history -r/-w/-a`, HISTFILE load on startup and save on exit)

## Prerequisites

Recommended environment: Linux x86_64 with CMake and GCC/Clang installed.

## Run

### Using a prebuilt release package

Run directly inside the extracted directory:

```sh
./shell
```

### Build from source and run

From the repository root:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
./build/shell
```

After startup, the shell runs in interactive mode.

## Basic Usage

### Builtins

- `pwd`: print current directory
- `cd <path>`: change directory (supports `cd ~`)
- `echo <text>`: print text
- `type <cmd>`: inspect command type
- `history`: show command history
- `exit`: exit the shell

### External Commands

Commands are discovered via PATH:

```sh
ls
cat README.md
```

### Redirection

```sh
echo hello > out.txt
echo world >> out.txt
```

### Pipelines

```sh
echo hello | wc -c
```

### TAB Completion

- First token: command completion
- Non-first token: file or directory path completion

### Persistent History

Set `HISTFILE` to load history on startup and save on exit:

```sh
HISTFILE=.my_shell_history ./build/shell
```

## Verification Script

The repository includes a smoke test script:

```sh
tests/shell_smoke_test.sh
```
