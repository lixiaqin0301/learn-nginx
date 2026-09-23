#!/usr/bin/env python3
"""预生成 clangd 后台索引（无需打开编辑器）。

用法:
    在含 compile_commands.json 的目录下直接运行 clangd-index，不接受任何参数。

索引写入 ./.cache/clangd/index/，每完成一个文件即落盘；
Ctrl-C 退出时已完成的索引会保留，下次继续。

只启动一个 clangd 进程，由它自己并行索引 CDB 里的全部翻译单元；
脚本仅发送 LSP 握手 + didOpen（打开第一个文件作为索引的触发点），
然后依据 clangd 上报的 $/progress(backgroundIndexProgress) 跟踪进度，
结束时发送 shutdown + exit 让它干净退出。
"""
import json
import os
import subprocess
import sys
import threading
import time
from pathlib import Path


def frame(obj):
    body = json.dumps(obj).encode()
    return b"Content-Length: %d\r\n\r\n" % len(body) + body


def main():
    cdb_dir = Path().absolute()
    cdb = cdb_dir/"compile_commands.json"
    entries = json.load(cdb.open())
    tus = sorted({e["file"] for e in entries if e.get("file")})
    index_dir = cdb_dir/".cache/clangd/index"
    index_dir.mkdir(755, True, True)
    print(f"编译数据库: {cdb} ({len(tus)} 个翻译单元)")
    print(f"索引目录  : {index_dir}")

    def count():
        return len([f for f in os.listdir(index_dir) if f.endswith(".idx")])

    before = count()
    proc = subprocess.Popen(
        ["clangd", "--background-index", f"--compile-commands-dir={cdb_dir}"],
        cwd=cdb_dir,
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        stderr=subprocess.DEVNULL,
    )

    # 握手/通知/停机都由主线程写，读取线程偶尔要回 server->client 请求，加锁串行化
    write_lock = threading.Lock()
    state = {"pct": None, "done": False}

    def send(*msgs):
        with write_lock:
            try:
                proc.stdin.write(b"".join(frame(m) for m in msgs))
                proc.stdin.flush()
            except Exception:
                pass

    def reader():
        """解析 clangd 的输出：跟踪 backgroundIndexProgress，并应答它的请求。"""
        f = proc.stdout
        while True:
            length = None
            while True:
                line = f.readline()
                if not line:
                    return
                if not line.strip():
                    break
                if line.lower().startswith(b"content-length:"):
                    try:
                        length = int(line.split(b":", 1)[1].strip())
                    except ValueError:
                        length = None
            if length is None:
                continue
            body = f.read(length)
            try:
                msg = json.loads(body)
            except ValueError:
                continue
            if msg.get("method") == "$/progress":
                params = msg.get("params") or {}
                if params.get("token") == "backgroundIndexProgress":
                    value = params.get("value") or {}
                    if value.get("kind") == "end":
                        state["done"] = True
                    if value.get("percentage") is not None:
                        state["pct"] = value["percentage"]
            elif "id" in msg and msg.get("method"):
                # 例如 window/workDoneProgress/create，必须应答，否则 clangd 不再上报进度
                send({"jsonrpc": "2.0", "id": msg["id"], "result": None})

    threading.Thread(target=reader, daemon=True).start()

    root = f"file://{cdb_dir}"
    first = tus[0]
    try:
        text = open(first, errors="replace").read()
    except OSError:
        text = ""
    send(
        {
            "jsonrpc": "2.0",
            "id": 1,
            "method": "initialize",
            "params": {
                "processId": os.getpid(),
                "rootUri": root,
                "workspaceFolders": [{"uri": root, "name": "project"}],
                "capabilities": {
                    "workspace": {"workspaceFolders": True, "configuration": True},
                    "window": {"workDoneProgress": True},
                },
            },
        },
        {"jsonrpc": "2.0", "method": "initialized", "params": {}},
        {"jsonrpc": "2.0", "method": "workspace/didChangeConfiguration", "params": {"settings": {}}},
        {
            "jsonrpc": "2.0",
            "method": "textDocument/didOpen",
            "params": {
                "textDocument": {
                    "uri": "file://" + first,
                    "languageId": "c",
                    "version": 1,
                    "text": text,
                }
            },
        },
    )
    print("已启动 clangd，后台索引中… (Ctrl-C 可中断，已完成的索引会保留)")

    def stop_gracefully():
        """按 LSP 规矩发 shutdown + exit，让 clangd 正常退出（退出码 0）。"""
        send(
            {"jsonrpc": "2.0", "id": 2, "method": "shutdown", "params": None},
            {"jsonrpc": "2.0", "method": "exit", "params": None},
        )
        try:
            code = proc.wait(timeout=10)
        except subprocess.TimeoutExpired:
            proc.terminate()
            code = proc.wait()
        try:
            proc.stdin.close()
        except Exception:
            pass
        return code

    last = -1
    stable = 0
    failed = False
    while True:
        time.sleep(1)
        done = count()
        if done != last:
            note = "" if state["pct"] is None else f"  索引进度 {state['pct']}%"
            print(f"  已索引 {done} 个文件{note}")
            last = done
            stable = 0
        else:
            stable += 1
        if state["done"]:
            print("clangd 上报索引完成")
            break
        if done > 0 and stable >= 5:
            # 兜底：万一没收到进度通知，连续 5 秒索引文件数不再增长就认为做完了
            print("索引文件数已停止增长，按完成处理")
            break
        if proc.poll() is not None:
            print(f"clangd 已退出（返回码 {proc.returncode}）")
            failed = True
            break

    code = stop_gracefully()
    total = count()
    print(f"完成：索引目录共 {total} 个文件（本次新增 {total - before}）")
    if code != 0:
        print(f"注意：clangd 退出码 {code}（非优雅退出）", file=sys.stderr)
    if failed:
        sys.exit(1)


if __name__ == "__main__":
    main()
