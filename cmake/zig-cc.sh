#!/bin/bash
exec /tmp/venv/bin/python3 -m ziglang cc -target x86_64-windows-gnu "$@"
