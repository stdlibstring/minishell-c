# CodeCrafters Shell (C) - Development Notes

This repository contains a single-file shell implementation in src/main.c,
built incrementally through CodeCrafters stages.

The project now includes:
- Interactive prompt input in raw terminal mode
- Builtins: echo, type, exit, pwd, cd, history
- PATH-based external command execution
- Quoting and escaping support for argument parsing
- Redirection parsing for stdout/stderr (overwrite + append)
- Pipeline execution with multiple stages
- TAB completion (command + file/path completion)
- In-memory history, history file read/write/append, startup/exit sync

## Quick Start

1. Build/run locally:

```sh
./your_program.sh
```

2. Submit to CodeCrafters:

```sh
codecrafters submit
```

## Architecture Overview

The implementation is intentionally organized by responsibility, even though it
is a single C file:

1. Input and terminal control
- enable_interactive_input_mode / restore_interactive_input_mode
- read_command_line and key handling helpers

2. Completion subsystem
- command completion from builtins + PATH executables
- argument completion from filesystem entries
- pending-list and common-prefix behavior for repeated TAB

3. Parsing subsystem
- parse_arguments for quoting/escaping
- split_command_and_redirections for redirect extraction

4. Execution subsystem
- run_builtin_command for builtins
- execute_external_command and PATH lookup
- execute_pipeline for multi-stage pipelines

5. History subsystem
- fixed-size in-memory storage
- history -r / -w / -a
- HISTFILE startup load and exit save

## Development Timeline

### 1) Core REPL + Builtins
- Established prompt loop and command dispatch
- Added minimal builtins first (echo, exit), then extended to type/pwd/cd

Reasoning:
- A stable REPL loop made later stages easier to isolate and verify

### 2) Command Parsing
- Added whitespace tokenization with quote-aware parsing
- Implemented escaping rules for outside quotes and inside double quotes

Reasoning:
- Parsing correctness is a prerequisite for redirection and history commands

### 3) Redirection
- Added support for >, >>, 1>, 1>>, 2>, 2>>
- Supported both attached form (for example 2>err.log) and split form

Reasoning:
- Parse first, then apply descriptors with rollback-safe logic

### 4) Completion
- Built command completion from builtins + PATH
- Built argument completion from directory entries
- Added behavior parity details:
   - bell on no match
   - common prefix expansion
   - repeated TAB to list candidates
   - directory suffix slash behavior

Reasoning:
- Completion behavior is stateful, so helper extraction was done to keep code
   maintainable as edge cases grew

### 5) Pipelines
- Implemented pipeline splitting by | token
- Added N-stage pipeline execution with proper pipe/fork lifecycle
- Allowed builtins inside pipeline children

Reasoning:
- A generalized N-stage loop avoids special-case code for two-command pipelines

### 6) History Features
- Added in-memory history list and numbered history output
- Added history <n> view
- Added arrow key navigation (up/down) with editable draft restoration
- Added history -r (read from file), history -w (overwrite file), history -a
   (append delta only)
- Added HISTFILE startup load and exit save

Reasoning:
- Introduced unsaved counter to make history -a idempotent across repeated calls
- Kept startup-loaded entries marked as already-saved

## Key Design Decisions

1. Fixed-size history buffer
- Simpler than dynamic allocation
- Predictable memory behavior
- Oldest entries are discarded on overflow

2. Unsaved-history delta tracking
- g_history_unsaved_count tracks how many latest entries are not persisted
- history -a appends only that suffix range
- Counter resets only on successful write

3. File IO tolerance
- Missing history files are treated as non-fatal
- Empty lines in history files are skipped in memory representation

4. Newline strategy
- Builtin output uses tty-aware newline handling
- History file persistence always writes one trailing newline per entry

## Refactor Notes

Recent cleanup extracted shared helpers for history IO:
- trim_line_endings
- load_history_from_file
- write_history_range_to_file

This removed duplication across:
- history -r
- history -w
- history -a
- HISTFILE startup/exit synchronization

## Regression Checklist

Use this quick list after non-trivial changes:

1. Core command execution
```sh
printf 'echo hello\nexit\n' | ./your_program.sh
```

2. Redirection
```sh
printf 'echo hi > out.txt\nexit\n' | ./your_program.sh
cat out.txt
```

3. Pipeline
```sh
printf 'echo hello | wc -c\nexit\n' | ./your_program.sh
```

4. History read/write/append
```sh
printf 'echo a\nhistory -w h.txt\nhistory -a h.txt\nexit\n' | ./your_program.sh
cat h.txt
```

5. HISTFILE startup/exit
```sh
HISTFILE=h.txt ./your_program.sh
```

## File Map

- src/main.c: all runtime logic
- your_program.sh: launcher script used by local run + tester

## Notes for Future Work

- Split main.c into modules (input.c, parse.c, history.c, exec.c) once challenge
   stages are complete
- Add lightweight local tests around parser and history persistence behaviors
