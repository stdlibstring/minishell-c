# minishell-c

[English](README.en.md) | 中文版

一个基于 C 语言实现的命令行 Shell。

说明：
- 源码仓库包含完整代码与构建文件。
- 预编译发布包仅包含可执行文件与使用文档，不包含源码目录。

本文档面向首次接触该项目的用户，提供功能说明与使用方式。

## 当前功能

- 交互输入（退格、上下方向键历史）
- 内建命令：echo、type、exit、pwd、cd、history
- 外部命令执行（PATH 搜索）
- 参数解析（引号、转义）
- 重定向（stdout/stderr，覆盖与追加）
- 管道执行
- TAB 补全（命令和文件路径）
- 历史持久化（history -r/-w/-a，HISTFILE 启动加载与退出保存）

## 使用前准备

建议环境：Linux x86_64，已安装 CMake 与 GCC/Clang。

## 运行

### 使用预编译发布包

在解压目录中直接运行：

```sh
./shell
```

### 从源码构建并运行

在仓库根目录执行：

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
./build/shell
```

启动后即可在交互模式中输入命令。

## 基本使用

### 内建命令

- `pwd`：显示当前目录
- `cd <path>`：切换目录（支持 `cd ~`）
- `echo <text>`：输出文本
- `type <cmd>`：查看命令类型
- `history`：查看历史记录
- `exit`：退出 shell

### 外部命令

支持通过 PATH 搜索并执行系统命令：

```sh
ls
cat README.md
```

### 重定向

```sh
echo hello > out.txt
echo world >> out.txt
```

### 管道

```sh
echo hello | wc -c
```

### TAB 补全

- 首个 token：补全命令名
- 非首个 token：补全文件或目录路径

### 历史持久化

设置 `HISTFILE` 后，启动时会加载历史，退出时会自动保存：

```sh
HISTFILE=.my_shell_history ./build/shell
```

## 验证脚本

项目内置了一个烟雾测试脚本：

```sh
tests/shell_smoke_test.sh
```
