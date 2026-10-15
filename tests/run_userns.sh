#!/usr/bin/env bash
# Run a test binary inside an unprivileged user+mount namespace so that the
# bind-mount cycle guard (G1) can be exercised without real root.
#
# Returns 77 (CTest SKIP_RETURN_CODE) if user namespaces are unavailable.
set -euo pipefail

if [ "$#" -ne 1 ]; then
    echo "usage: $0 <test-binary>" >&2
    exit 2
fi

BINARY="$1"

if ! command -v unshare >/dev/null 2>&1; then
    echo "SKIP: unshare not available" >&2
    exit 77
fi

# Probe whether an unprivileged user+mount namespace can be created.
if ! unshare -rm true >/dev/null 2>&1; then
    echo "SKIP: unprivileged user+mount namespaces unavailable" >&2
    exit 77
fi

exec unshare -rm "$BINARY"
