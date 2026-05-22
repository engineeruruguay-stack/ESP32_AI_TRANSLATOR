#!/usr/bin/env bash
set -e

if [ -z "$1" ]; then
    echo "Использование: $0 <port>"
    echo "Пример: $0 /dev/ttyUSB0"
    exit 1
fi

PROJECT_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$PROJECT_ROOT"

idf.py -p "$1" flash monitor
