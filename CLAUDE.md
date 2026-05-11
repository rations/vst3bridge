# VST3 Bridge — Developer Reference

## What This Project Does

`vst3bridge` is a Wine-based VST3 bridge that lets Windows VST3 plugins run on Linux (X11 only, no Wayland). It is designed to work with **current winehq-staging** and must not depend on any specific Wine version number.

It does **not** require systemd — it uses only POSIX primitives (`shm_open`, Unix sockets, `fork`/`exec`) and works on Devuan/sysvinit and any other init system.

**Target DAW for testing:** Reaper on Devuan GNU/Linux.
**Test plugin:** NA Black.vst3 at `~/.wine/drive_c/Program Files/Common Files/VST3/NA Black.vst3/`

---

## Architecture

```
DAW (Reaper)
  │  VST3 API
  ▼
libvst3bridge.so          ← native Linux VST3 plugin (.so in DAW scan path)
  │  fork+exec wine
  │  Unix socket (AF_UNIX, /tmp/vst3bridge_<pid>_*)
  │  POSIX SHM (audio buffers, video frames)
  ▼
vst3bridge-host.exe       ← Wine PE executable (actually ELF, runs under wine)
  │  LoadLibraryW
  ▼
Windows .vst3 DLL         ← real Windows plugin (in Wine prefix)
```

**Native side** (`src/native/`): Implements `IPluginFactory` and `IEditController`/`IComponent` proxies. Receives VST3 calls from the DAW, serializes them over a Unix socket to the Wine host.

**Wine host** (`src/wine-host/`): Connects to the Unix socket, loads the Windows DLL via `LoadLibraryW`, and calls the real VST3 interfaces. GDI captures the plugin GUI and sends frames via shared memory.

**Common** (`src/common/`): Protocol definitions, socket wrapper, shared memory abstractions.

---

## Directory Structure

```
vst3bridge/
├── src/
│   ├── common/             Unix socket, POSIX SHM, IPC protocol, stream impl
│   │   ├── protocol.h      All message structs and MsgType enum
│   │   ├── socket.h/cpp    MessageSocket + SocketServer (AF_UNIX on Linux)
│   │   ├── shared_memory.h POSIX SHM wrappers (AudioSharedMemory, FrameSharedMemory)
│   │   └── logger.h        Thread-safe logging
│   ├── native/             Linux-side: loaded by DAW as a real VST3 plugin
│   │   ├── vst3_entry.cpp  GetPluginFactory() — forks Wine host, returns factory proxy
│   │   ├── plugin_factory.cpp  IPluginFactory proxy (routes to Wine)
│   │   ├── plugin_proxy.cpp    IComponent/IAudioProcessor/IEditController proxy
│   │   ├── x11_window.cpp  X11 window for embedding plugin GUI
│   │   ├── gui_handler.cpp XCB event capture → sends to Wine as MsgInputEvent
│   │   └── audio_shm.cpp   Native-side audio SHM client
│   └── wine-host/          Wine-side: PE executable running under wine
│       ├── host_main.cpp   Main loop: Windows message pump + IPC dispatch
│       ├── vst3_host.cpp   LoadLibraryW, GetPluginFactory, IPluginFactory2/3
│       ├── plugin_instance.cpp  Creates/manages plugin instances via QueryInterface
│       ├── audio_processor.cpp  Runs IAudioProcessor::process() per block
│       ├── gdi_capture.cpp GDI BitBlt → writes BGRA frames to FrameSharedMemory
│       ├── window_manager.cpp  Off-screen HWND creation for plugin GUI
│       ├── component_handler.cpp  IComponentHandler proxy (Wine→native callbacks)
│       ├── parameter_changes.h  ParameterChanges/ParamValueQueue implementations
│       ├── host_application.h   IHostApplication impl (returns "VST3 Bridge")
│       ├── wine_socket_client.h  WineSocketClient declaration (AF_UNIX via Winsock2)
│       ├── socket_server.cpp    WineSocketClient implementation (AF_UNIX via Winsock2)
│       └── wine_win32_prelude.h  Force-included first in every Wine TU (see below)
├── third_party/
│   ├── vst3sdk/            Steinberg VST3 SDK v3.7.3 (git submodule)
│   └── wine-windows/include/  Vendored Wine Windows headers from libwine-dev 10.0
├── meson.build             Root build file (native .so + Wine .exe)
├── wine.cross              Meson cross-file for Wine/wineg++ builds
└── meson_options.txt       Options: bitbridge, winedbg, tests
```

---

## Prerequisites

```bash
# Devuan / Debian
apt install meson ninja-build g++ \
    libx11-dev libxcb1-dev libxcb-keysyms1-dev libxext-dev \
    winehq-staging  # or wine-staging from your distro
```

No systemd or DBus required.

---

## Build

```bash
# First time only — initialize the VST3 SDK submodule
git submodule update --init third_party/vst3sdk

# Build everything (native .so + Wine .exe in one pass)
meson setup build
ninja -C build
# → build/libvst3bridge.so        (native Linux plugin)
# → build/vst3bridge-host.exe     (Wine PE stub launcher)
# → build/vst3bridge-host.exe.so  (Wine ELF shared object, actual code)
```

Both targets are built by `ninja -C build` in one pass if `wineg++` is found.

---

## Running / Testing with Reaper

1. Install the build output:
   ```bash
   mkdir -p ~/.local/share/vst3bridge
   cp build/vst3bridge-host.exe ~/.local/share/vst3bridge/
   cp build/vst3bridge-host.exe.so ~/.local/share/vst3bridge/
   ```

2. Create a `.vst3` bundle for Reaper to find:
   ```bash
   mkdir -p ~/.vst3/vst3bridge.vst3/Contents/x86_64-linux
   cp build/libvst3bridge.so ~/.vst3/vst3bridge.vst3/Contents/x86_64-linux/vst3bridge.so
   ```

3. Set the plugin path and launch Reaper (temporary, until Wine prefix scanning is implemented):
   ```bash
   VST3BRIDGE_PLUGIN_PATH="/home/human/.wine/drive_c/Program Files/Common Files/VST3/NA Black.vst3/Contents/x86_64-win/NA Black.vst3" \
   WINEPREFIX="$HOME/.wine" \
   reaper
   ```

4. In Reaper: *Preferences → Plug-ins → VST → VST3 plug-in path* → add `~/.vst3` → *Re-scan*.

---

## Key Environment Variables

| Variable | Purpose | Default |
|----------|---------|---------|
| `WINEPREFIX` | Wine prefix to use | `~/.wine` |
| `VST3BRIDGE_PLUGIN_PATH` | Single Windows .vst3 DLL path (temporary) | scan Wine prefix |
| `VST3BRIDGE_SOCKET_PATH` | IPC socket path (set by native side, read by Wine host) | auto `/tmp/vst3bridge_<pid>_*` |
| `VST3BRIDGE_SHM_NAME` | Audio SHM name (set by native, read by Wine host) | auto `/vst3bridge_<pid>_*` |

---

## IPC Protocol

All communication is over a `AF_UNIX` stream socket using a fixed 24-byte `MessageHeader`:

```
magic (4) | version (2) | pad (2) | type (4) | payload_size (4) | seq (8)
```

Followed by `payload_size` bytes of a typed payload struct. All structs are in [src/common/protocol.h](src/common/protocol.h).

**Audio data** goes via POSIX shared memory (not the socket) for performance. The socket carries only the synchronization signals (`MsgRequestProcess` / `MsgResponseProcess`).

**GUI frames** go via a separate `FrameSharedMemory` triple-buffer ring. Frame metadata (`MsgFrameUpdate`) is sent over the socket.

---

## Wine Build Notes — `wine_win32_prelude.h`

The Wine host is compiled with `wineg++` (GCC targeting Wine/PE). Several issues arise from the interaction between wineg++, glibc 2.38+, and the Steinberg VST3 SDK headers. All are fixed by force-including `src/wine-host/wine_win32_prelude.h` via `-include` in meson.build.

**Why it exists and what it does:**

1. **`winsock2.h` / `sys/select.h` conflict** — glibc 2.38+ has a chain `stdlib.h → sys/types.h → sys/select.h` that defines `FD_CLR` as an error sentinel before `winsock2.h` can claim `fd_set`. The prelude includes `<winsock2.h>` first, before any C++ standard library header in any translation unit.

2. **`SMTG_DEPRECATED_ATTRIBUTE` parse error** — wineg++ defines `-D __declspec(x)=__declspec_##x` and `-D __declspec_deprecated=__attribute__((deprecated))`. The SDK's `__declspec(deprecated("msg"))` token-pastes to `__declspec_deprecated("msg")`, which then calls an object-like macro as a function. Fixed by redefining `__declspec_deprecated(...)` as `__attribute__((deprecated(...)))`.

3. **Windows string functions** — The SDK's `fstring.cpp` Windows code path uses `stricmp`, `strnicmp`, `wcsicmp`, `wcsnicmp`, `_vsnwprintf`. These are not declared by the vendored Wine headers. The prelude provides compat `#define` aliases to POSIX equivalents (`strcasecmp`, etc.).

4. **`FoldString` ANSI vs. Unicode** — The SDK's `ftypes.h` defines `UNICODE 1`, but only after `windows.h` has already resolved `WINELIB_NAME_AW(FoldString)` to `FoldStringA`. The prelude defines `UNICODE 1` before including any Windows headers so all `*W` variants are selected.

---

## Vendored Wine Headers (`third_party/wine-windows/`)

The Windows API headers used by `wineg++` live at `third_party/wine-windows/include/`. They are the **pre-generated** headers from the `libwine-dev 10.0` Debian package — not raw Wine source. The upstream `wine-mirror/wine` repo only ships `.idl` source files that require `widl` to generate the `.h` counterparts, so it cannot be used as a submodule directly.

Vendoring is the correct cross-distro approach: anyone can `git clone` the repo and build without installing `libwine-dev`, and the headers are consistent across Debian, Fedora, Arch, etc.

**To update to a newer Wine version:**
1. Download the new `libwine-dev` `.deb` from packages.debian.org (or equivalent)
2. `dpkg -x libwine-dev_<version>.deb /tmp/wine-dev`
3. `rsync -a --delete /tmp/wine-dev/usr/include/wine/wine/windows/ third_party/wine-windows/include/`
4. Test the build, then commit

**Future:** once the project matures, create `github.com/rations/wine-windows-headers` containing only these pre-generated headers and convert `third_party/wine-windows` to a proper submodule pointing there.

---

## SDK Source Files Included in Build

In addition to the standard SDK files, `meson.build` includes:

- `third_party/vst3sdk/base/thread/source/flock.cpp` — required by `fobject.cpp` for singleton locking; not included in the SDK's default CMake targets for non-MSVC builds.

---

## Known Issues / Current Status

- **Wine prefix auto-scanning not yet implemented** — use `VST3BRIDGE_PLUGIN_PATH` env var to specify a single plugin DLL path for testing (see Running section above).
- **Audio processing handshake** — the `AudioReady` / `Process` / `ResponseProcess` flow needs end-to-end testing once the full IPC round-trip is exercised with a real plugin.
- **GUI embedding** — X11 window embedding and GDI frame capture compile but have not been tested end-to-end.

---

## Threading Model

**Native process:**
- DAW audio thread → `PluginProxy::process()` (real-time, no allocation, no blocking)
- DAW GUI thread → all other VST3 calls
- Socket I/O: mutex-protected, shared between threads

**Wine host process:**
- Main thread: Windows message pump interleaved with IPC message dispatch
- GDI capture thread: ~30fps, writes to `FrameSharedMemory`
- Audio processing runs on main thread (avoids cross-thread Win32 issues)

---

## File Naming Conventions

- `Msg*` structs — IPC message payload types (in `protocol.h`)
- `*Proxy` classes — native-side proxies (forward calls to Wine host)
- `*Host` / `*Instance` classes — Wine-side implementations (call real VST3)
- `*Shm` / `*SharedMemory` — POSIX shared memory wrappers
