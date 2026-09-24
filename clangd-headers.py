#!/usr/bin/env python3
"""给 compile_commands.json 补充头文件条目，消除 .h 的假阳性。

背景
----
nginx 的头文件不是自包含的，而且存在环。以 src/core/ngx_list.h 为例：

    ngx_list.h:13   #include <ngx_core.h>
    ngx_core.h:68   #include <ngx_list.h>     ← 构成环
    ngx_core.h:86   #include <ngx_cycle.h>
    ngx_cycle.h:67  使用 ngx_list_t           ← 由 ngx_list.h 定义

环靠 include guard 断开，于是"谁先被进入"决定了谁变成空操作：

  · 从 .c 进入（先 ngx_core.h）：ngx_list.h 在 ngx_cycle.h 之前展开，正常；
  · 直接检查 ngx_list.h：ngx_list.h:13 先把 ngx_core.h 拉起来，
    ngx_core.h:68 再包含 ngx_list.h 时 guard 已置位 → 空操作，
    于是 ngx_cycle.h:67 看到未定义的 ngx_list_t → 假阳性。

做法
----
按目录推导"伞头文件"——该目录的 .c 文件在首个系统头之前最后包含的那个项目头文件
（src/core → ngx_core.h，src/http → ngx_http.h， src/event/quic → ngx_event_quic_connection.h），
为每个 .h 生成一条

    -x c-header -include <伞头文件> <该头文件>

的编译命令，让头文件总是先经过正确的入口点。之后 `clang-check -p .` 与clangd 处理头文件就都正常了。

用法
----
在含 compile_commands.json 的目录下运行：

    clangd-headers              # 生成/刷新头文件条目（原地改写 CDB）
    clangd-headers --verify     # 生成后逐个跑 clang-check 复核
    clangd-headers --dry-run    # 只打印计划，不写文件

注意
----
compile_commands.json 由 `bear -- make` 生成，而 bear 不会给头文件生成条目；
该文件也在 .gitignore 里。所以每次重新生成 CDB 之后都要再跑一次本脚本。
脚本是幂等的：重复运行不会累积重复条目。
"""
import argparse
import collections
import json
import os
import re
import shlex
import shutil
import subprocess
import sys
from pathlib import Path

# 本平台用不到的头文件，跳过。两类：
#   · 别的操作系统：引用了 BSD/Solaris 才有的头（如 <sys/filio.h>）或 BSD 专有宏；
#   · 别的架构的原子操作实现：ngx_atomic.h 用 #if 从这几个变体里挑一个，
#     单独检查未选中的变体会撞上"ngx_memory_barrier 宏重定义"。
# 若 --verify 报出新失败，多半是 nginx 新增了平台/架构专用头，把前缀加到这里。
SKIP_PREFIXES = (
    "ngx_darwin",          # macOS
    "ngx_freebsd",         # FreeBSD
    "ngx_solaris",         # Solaris
    "ngx_gcc_atomic_",     # 原子操作变体，由 ngx_atomic.h 按架构选择
    "ngx_sunpro_atomic_",
)

INC_RE = re.compile(r"^\s*#\s*include\s*<([^>]+)>")


def entry_args(entry):
    """返回条目的参数列表，兼容 arguments / command 两种写法。"""
    if "arguments" in entry:
        return list(entry["arguments"])
    return shlex.split(entry["command"])


def compile_flags(entry):
    """去掉 argv[0]、-c、-o <out> 和源文件，只留纯编译选项。

    必须去掉 -o：nginx 用 -Werror 构建，残留的 -o 会触发 `-Werror,-Wunused-command-line-argument` 而根本不去解析源码。
    """
    out, skip = [], False
    for a in entry_args(entry)[1:]:
        if skip:
            skip = False
            continue
        if a == "-o":
            skip = True
            continue
        if a == "-c" or a.endswith((".c", ".cc", ".cpp")):
            continue
        out.append(a)
    return out


def include_dirs(cdb):
    """收集全部 -I 目录（绝对路径），用于判断某个 #include 是不是项目头文件。"""
    dirs = []
    for e in cdb:
        args = entry_args(e)
        base = e["directory"]
        for i, a in enumerate(args):
            if a == "-I" and i + 1 < len(args):
                dirs.append(os.path.join(base, args[i + 1]))
            elif a.startswith("-I") and len(a) > 2:
                dirs.append(os.path.join(base, a[2:]))
    return dirs


def resolve_header(name, inc_dirs):
    for d in inc_dirs:
        p = os.path.join(d, name)
        if os.path.isfile(p):
            return os.path.realpath(p)
    return None


def leading_includes(path):
    """文件开头那一段连续 #include <...> 里的头文件名。

    遇到第一个非空且非 include 的行就停止，避免把函数体里条件包含的模块头文件误当成伞头文件。
    """
    names = []
    for line in path.read_text(errors="ignore").splitlines():
        m = INC_RE.match(line)
        if m:
            names.append(m.group(1))
        elif names and line.strip():
            break
    return names


def derive_umbrellas(c_sources, inc_dirs):
    """目录 -> 伞头文件绝对路径。由该目录的 .c 文件投票得出。"""
    by_dir = collections.defaultdict(list)
    for e in c_sources:
        by_dir[os.path.dirname(e["file"])].append(e)

    umbrellas = {}
    for d, entries in by_dir.items():
        votes = collections.Counter()
        for e in entries:
            proj = [n for n in leading_includes(Path(e["file"])) if resolve_header(n, inc_dirs)]
            if proj:
                votes[proj[-1]] += 1
        if votes:
            umbrellas[d] = resolve_header(votes.most_common(1)[0][0], inc_dirs)
    return umbrellas


def main():
    ap = argparse.ArgumentParser(description="给 compile_commands.json 补充头文件条目，消除 .h 的假阳性")
    ap.add_argument("--verify", action="store_true", help="生成后逐个头文件跑 clang-check 复核")
    ap.add_argument("--dry-run", action="store_true", help="只打印计划，不写文件")
    args = ap.parse_args()

    root = Path.cwd()
    cdb_path = root / "compile_commands.json"
    if not cdb_path.is_file():
        sys.exit(f"找不到 {cdb_path}")

    cdb = json.load(cdb_path.open())
    # 丢掉上一次生成的头文件条目，保证幂等
    c_sources = [e for e in cdb if not e["file"].endswith(".h")]
    dropped = len(cdb) - len(c_sources)

    inc_dirs = include_dirs(c_sources)
    umbrellas = derive_umbrellas(c_sources, inc_dirs)

    # 模板取同目录的 .c：各目录的 -I 集合不同（src/http 下多 4 个）
    templates = {}
    for e in c_sources:
        templates.setdefault(os.path.dirname(e["file"]), e)

    def template_for(d):
        cur = d
        while cur.startswith(str(root)):
            if cur in templates:
                return templates[cur]
            parent = os.path.dirname(cur)
            if parent == cur:
                break
            cur = parent
        return None

    added, skipped = [], []
    for d in sorted(umbrellas):
        tpl = template_for(d)
        if tpl is None:
            continue
        umbrella = umbrellas[d]
        flags = compile_flags(tpl)
        argv0 = entry_args(tpl)[0]
        for name in sorted(os.listdir(d)):
            if not name.endswith(".h"):
                continue
            if name.startswith(SKIP_PREFIXES):
                skipped.append(os.path.join(d, name))
                continue
            hdr = os.path.join(d, name)
            added.append({
                "directory": tpl["directory"],
                "file": hdr,
                "arguments": [argv0, "-x", "c-header"] + flags + ["-include", umbrella, hdr],
            })

    print(f"编译数据库: {cdb_path}")
    print("伞头文件  :")
    for d in sorted(umbrellas):
        print(f"    {os.path.relpath(d, root):<24} -> {os.path.relpath(umbrellas[d], root)}")
    print(f"头文件条目: {len(added)} 个" + (f"（替换掉上次生成的 {dropped} 个）" if dropped else ""))
    print(f".c 条目   : {len(c_sources)} 个（保留不动）")
    if skipped:
        print(f"已跳过    : {len(skipped)} 个非本平台头文件")
        for s in skipped:
            print(f"    {os.path.relpath(s, root)}")

    if args.dry_run:
        print("\n--dry-run：未写入。")
        return

    merged = c_sources + added
    tmp = cdb_path.with_suffix(".json.tmp")
    tmp.write_text(json.dumps(merged, indent=1) + "\n")
    tmp.replace(cdb_path)
    print(f"\n已写入 {cdb_path}")

    if not args.verify:
        return

    if not shutil.which("clang-check"):
        sys.exit("\n找不到 clang-check，无法复核（apt install clang-tools）")

    print(f"\n复核 {len(added)} 个头文件 ...")
    bad = []
    for e in added:
        r = subprocess.run(["clang-check", "-p", str(root), e["file"]],
                           capture_output=True, text=True)
        if r.returncode != 0:
            bad.append((e["file"], (r.stderr or r.stdout).strip()))
    if not bad:
        print(f"全部通过：{len(added)}/{len(added)} 无报错")
        return
    print(f"仍有 {len(bad)} 个失败：")
    for f, msg in bad:
        print(f"\n  {os.path.relpath(f, root)}")
        for line in msg.splitlines()[:4]:
            print(f"      {line}")
    sys.exit(1)


if __name__ == "__main__":
    main()
