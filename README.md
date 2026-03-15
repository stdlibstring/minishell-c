# CodeCrafters Shell (C)

[English](README.en.md) | 中文版

跨语言精准跳转：
- Features / 功能概览: [EN](README.en.md#features) | [ZH](README.md#features)
- Quick Start / 快速开始: [EN](README.en.md#quick-start) | [ZH](README.md#quick-start)
- Behavior Notes / 行为说明: [EN](README.en.md#behavior-notes) | [ZH](README.md#behavior-notes)
- Architecture Layers / 架构分层: [EN](README.en.md#architecture-layers) | [ZH](README.md#architecture-layers)
- Function-Level Index / 函数级目录索引: [EN](README.en.md#function-level-index) | [ZH](README.md#function-level-index)
- Debugging and Regression / 调试与回归: [EN](README.en.md#debugging-and-regression) | [ZH](README.md#debugging-and-regression)
- Design Trade-offs / 设计取舍: [EN](README.en.md#design-trade-offs) | [ZH](README.md#design-trade-offs)
- Next Improvements / 后续优化: [EN](README.en.md#next-improvements) | [ZH](README.md#next-improvements)

一个基于 C 实现的单文件 Shell，当前核心代码位于 [src/main.c](src/main.c)。

目标定位：
- 通过 CodeCrafters Shell 挑战阶段测试
- 保持功能完整的同时尽量清晰、可维护
- 为后续模块化拆分保留结构边界

## 目录

1. 功能概览
2. 快速开始
3. 行为说明
4. 架构分层
5. 函数级目录索引
6. 调试与回归
7. 设计取舍
8. 后续优化

<a id="features"></a>
## 功能概览

当前已实现：
- 交互式输入（raw mode、退格、上下方向键历史）
- 内建命令：echo、type、exit、pwd、cd、history
- PATH 搜索并执行外部命令
- 参数解析（引号、转义）
- 重定向（stdout/stderr，覆盖与追加）
- 多级管道执行
- TAB 补全（命令补全 + 文件路径补全）
- 历史记录系统：内存历史、history -r/-w/-a、HISTFILE 启停同步

<a id="quick-start"></a>
## 快速开始

本地运行：

```sh
./your_program.sh
```

提交评测：

```sh
codecrafters submit
```

<a id="behavior-notes"></a>
## 行为说明

### History 相关

- `history -r <path>`：读取文件并追加到内存历史
- `history -w <path>`：将当前内存历史全量覆盖写入文件
- `history -a <path>`：仅追加“上次成功落盘后新增”的历史
- `HISTFILE=<path> ./your_program.sh`：启动时自动加载，exit 时自动回写

实现约束：
- 内存历史是定长缓冲区，超限后丢弃最旧记录
- 空行不会进入内存历史
- 写历史时按“每条命令一行”格式输出，始终带换行

### 补全相关

- 首 token：命令补全（builtin + PATH 可执行文件）
- 非首 token：路径补全（文件/目录）
- 多匹配时：支持公共前缀扩展和重复 TAB 列表输出

<a id="architecture-layers"></a>
## 架构分层

虽然是单文件实现，但按职责可分为 5 层：

1. 输入与终端控制
- raw mode 切换
- 行编辑与方向键导航

2. 补全子系统
- 候选收集、去重、排序
- 前缀匹配与公共前缀策略

3. 解析子系统
- 参数解析（引号、转义）
- 重定向分离与规范化

4. 执行子系统
- builtin 派发
- 外部命令执行
- 多进程管道

5. 历史子系统
- 内存历史管理
- 历史文件读写
- 启停同步

<a id="function-level-index"></a>
## 函数级目录索引

下面索引与 [src/main.c](src/main.c) 保持一致。

1. 输入与终端
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

2. 补全
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

3. 历史
- history_append_entry
- trim_crlf_tail
- history_write_range_to_file
- history_load_from_file
- handle_history
- history_load_from_histfile_env
- history_write_to_histfile_env

4. 解析与重定向
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

5. 命令执行
- is_builtin
- find_executable_on_path
- handle_type
- handle_cd
- handle_echo
- handle_pwd
- execute_external_via_path
- run_builtin_command
- exec_external_in_child

6. 管道与分发
- find_pipe_separator_index
- count_argv_entries
- split_pipeline_argv
- execute_pipeline_commands
- execute_single_command

<a id="debugging-and-regression"></a>
## 调试与回归

推荐每次较大改动后执行以下最小回归：

1. 基础执行

```sh
printf 'echo hello\nexit\n' | ./your_program.sh
```

2. 重定向

```sh
printf 'echo hi > out.txt\nexit\n' | ./your_program.sh
cat out.txt
```

3. 管道

```sh
printf 'echo hello | wc -c\nexit\n' | ./your_program.sh
```

4. 历史读写

```sh
printf 'echo a\nhistory -w h.txt\nhistory -a h.txt\nexit\n' | ./your_program.sh
cat h.txt
```

5. HISTFILE 启停同步

```sh
HISTFILE=h.txt ./your_program.sh
```

<a id="design-trade-offs"></a>
## 设计取舍

1. 固定长度历史缓冲区
- 优点：实现简单、内存可控
- 代价：超限后会丢弃最旧历史

2. 增量落盘计数（unsaved counter）
- 优点：`history -a` 可重复执行且避免重复追加
- 代价：需要在所有写路径保持一致语义

3. 单文件组织
- 优点：挑战阶段开发速度快、定位直接
- 代价：文件体积大，长期维护成本上升

<a id="next-improvements"></a>
## 后续优化

建议下一步优先级：

1. 拆分模块
- 将 [src/main.c](src/main.c) 拆为 input/parse/history/exec 模块

2. 增加轻量测试脚本
- 覆盖 parser、history 持久化、pipeline 关键路径

3. 注释再标准化
- 为核心函数补全统一 Doxygen `@param`/`@return`
