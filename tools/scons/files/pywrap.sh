#!/usr/bin/env bash

case "${0##*/}" in
    pywrap.sh)
        arg1=""
        ;;
    *)
        arg1="$0.py"
        ;;
esac

for bin in python3 python3.14 python3.13 python3.12 python3.11 python3.10 python3.9; do
    if command -v "$bin" >/dev/null 2>&1; then
        if [ -n "$arg1" ]; then
            exec "$bin" "$arg1" "$@"
        else
            exec "$bin" "$@"
        fi
    fi
done

echo "Unable to find a Python 3.x interpreter for executing ${arg1:+$arg1 }$@ !" >&2
exit 1
