# CodeCrafters Shell (C)

English | [中文版](README.md)

Cross-language deep links:
- Features / 功能概览: [EN](README.en.md#features) | [ZH](README.md#features)
- Quick Start / 快速开始: [EN](README.en.md#quick-start) | [ZH](README.md#quick-start)
- Behavior Notes / 行为说明: [EN](README.en.md#behavior-notes) | [ZH](README.md#behavior-notes)
- Architecture Layers / 架构分层: [EN](README.en.md#architecture-layers) | [ZH](README.md#architecture-layers)
- Function-Level Index / 函数级目录索引: [EN](README.en.md#function-level-index) | [ZH](README.md#function-level-index)
- Debugging and Regression / 调试与回归: [EN](README.en.md#debugging-and-regression) | [ZH](README.md#debugging-and-regression)
- Design Trade-offs / 设计取舍: [EN](README.en.md#design-trade-offs) | [ZH](README.md#design-trade-offs)
- Next Improvements / 后续优化: [EN](README.en.md#next-improvements) | [ZH](README.md#next-improvements)

A single-file shell implementation in C. The core runtime lives in [src/main.c](src/main.c).

Project goals:
- Pass CodeCrafters Shell challenge stages
- Keep features complete while staying maintainable
- Preserve clear boundaries for future modularization

## Table of Contents

1. Features
2. Quick Start
3. Behavior Notes
4. Architecture Layers
5. Function-Level Index
6. Debugging and Regression
7. Design Trade-offs
8. Next Improvements

<a id="features"></a>
## Features

Implemented so far:
- Interactive input (raw mode, backspace, up/down history navigation)
- Builtins: echo, type, exit, pwd, cd, history
- PATH lookup and external command execution
- Argument parsing (quotes and escaping)
- Redirection (stdout/stderr, overwrite and append)
- Multi-stage pipeline execution
- TAB completion (command completion and file/path completion)
- History system: in-memory history, history -r/-w/-a, HISTFILE load/save

<a id="quick-start"></a>
## Quick Start

Run locally:

```sh
./your_program.sh
```

Submit to CodeCrafters:

```sh
codecrafters submit
```

<a id="behavior-notes"></a>
## Behavior Notes

### History

- `history -r <path>`: read commands from file and append to in-memory history
- `history -w <path>`: overwrite file with full in-memory history
- `history -a <path>`: append only entries added since last successful save
- `HISTFILE=<path> ./your_program.sh`: load on startup, save on exit

Constraints:
- In-memory history is fixed-size and drops oldest entries on overflow
- Empty lines are ignored when loading history
- History persistence writes one command per line with trailing newline

### Completion

- First token: command completion (builtins + PATH executables)
- Non-first token: filesystem path completion
- Multiple matches: longest common prefix expansion + repeated TAB list output

<a id="architecture-layers"></a>
## Architecture Layers

Even as a single-file implementation, the code can be viewed as 5 layers:

1. Input and terminal control
- Raw mode switching
- Line editing and history key navigation

2. Completion subsystem
- Candidate collection, dedup, sorting
- Prefix matching and longest-common-prefix logic

3. Parsing subsystem
- Argument parsing with quote/escape rules
- Redirection separation and normalization

4. Execution subsystem
- Builtin dispatch
- External command execution
- Multi-process pipelines

5. History subsystem
- In-memory history management
- History file read/write
- Startup/exit synchronization

<a id="function-level-index"></a>
## Function-Level Index

This index is aligned with [src/main.c](src/main.c).

1. Input and terminal
- is_space_or_tab
- print_newline_for_shell_io
- redraw_prompt_line
- read_ansi_escape_suffix
- apply_history_selection
- history_navigate_up
- history_navigate_down
- enable_interactive_input_mode
- restore_interactive_input_mode
- read_command_line

2. Completion
- has_prefix
- compare_strings
- compare_file_completion_entries
- longest_common_prefix_length_for_entries
- reset_tab_completion_state
- duplicate_path_env
- append_unique_match
- free_matches
- free_file_completion_entries
- append_completion_span
- append_completion_char
- print_command_match_list
- print_file_match_list
- set_pending_completion
- common_prefix_length
- longest_common_prefix_length
- collect_external_completion_matches
- collect_completion_matches
- collect_file_completion_entries
- is_first_token_position
- autocomplete_command_live

3. History
- history_append_entry
- trim_crlf_tail
- history_write_range_to_file
- history_load_from_file
- handle_history
- history_load_from_histfile_env
- history_write_to_histfile_env

4. Parsing and redirection
- is_escapable_in_double_quotes
- parse_arguments
- assign_redirection
- parse_inline_redirection_token
- parse_separate_redirection_token
- split_command_and_redirections
- redirect_fd_to_file
- apply_redirections
- restore_redirections
- restore_fd

5. Command execution
- is_builtin
- find_executable_on_path
- handle_type
- handle_cd
- handle_echo
- handle_pwd
- execute_external_via_path
- run_builtin_command
- exec_external_in_child

6. Pipelines and dispatch
- find_pipe_separator_index
- count_argv_entries
- split_pipeline_argv
- execute_pipeline_commands
- execute_single_command

<a id="debugging-and-regression"></a>
## Debugging and Regression

Recommended minimal checks after non-trivial changes:

1. Basic execution

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

5. HISTFILE startup/exit sync

```sh
HISTFILE=h.txt ./your_program.sh
```

<a id="design-trade-offs"></a>
## Design Trade-offs

1. Fixed-size history buffer
- Pros: simple and memory-bounded
- Cons: oldest entries are dropped after overflow

2. Unsaved-history delta counter
- Pros: enables idempotent `history -a` behavior
- Cons: all persistence paths must keep counter semantics consistent

3. Single-file organization
- Pros: fast iteration during challenge stages
- Cons: long-term maintenance cost increases with file size

<a id="next-improvements"></a>
## Next Improvements

Suggested next priorities:

1. Modular split
- Split [src/main.c](src/main.c) into input/parse/history/exec modules

2. Lightweight local tests
- Cover parser edge cases, history persistence, and pipeline behavior

3. Comment standardization
- Add consistent Doxygen `@param` and `@return` for core functions
