#!/usr/bin/env python3
"""本地静态服务器（带禁用缓存）。

为什么要自己写：`python -m http.server` 不发 Cache-Control，
改了 app.js / sgs.wasm 之后浏览器经常还拿旧缓存，表现就是"改了没反应"。

用法: python web/serve.py [端口]    然后打开 http://localhost:8000
"""
import functools
import http.server
import os
import socketserver
import sys


class NoCacheHandler(http.server.SimpleHTTPRequestHandler):
    def end_headers(self):
        self.send_header('Cache-Control', 'no-store, no-cache, must-revalidate, max-age=0')
        self.send_header('Pragma', 'no-cache')
        self.send_header('Expires', '0')
        # WebAssembly 用到的类型
        if self.path.endswith('.wasm'):
            self.send_header('Content-Type', 'application/wasm')
        super().end_headers()

    def log_message(self, fmt, *args):      # 少刷屏
        if not fmt.startswith('"GET /sgs'):
            sys.stderr.write("%s - %s\n" % (self.address_string(), fmt % args))


def main():
    port = int(sys.argv[1]) if len(sys.argv) > 1 else 8000
    root = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'dist')
    handler = functools.partial(NoCacheHandler, directory=root)
    socketserver.TCPServer.allow_reuse_address = True
    with socketserver.TCPServer(('0.0.0.0', port), handler) as httpd:
        print(f'serving {root} at http://localhost:{port}  (Cache-Control: no-store)')
        try:
            httpd.serve_forever()
        except KeyboardInterrupt:
            pass


if __name__ == '__main__':
    main()
