# AppImage runtime provenance

`runtime-x86_64` is the official type 2 AppImage runtime, pinned to upstream
commit `8f39b89e2ac31e1640b3d3f7e9a5108e6ce805fa`. It is included in the
Risco AppImage and is not application code.

Upstream: <https://github.com/AppImage/type2-runtime>

SHA-256 values for the runtime and its license are recorded in `SHA256SUMS`.
The package builder checks them before invoking linuxdeploy.
