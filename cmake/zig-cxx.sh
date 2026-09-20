#!/bin/bash
exec /tmp/venv/bin/python3 -m ziglang c++ -target x86_64-windows-gnu "$@"
