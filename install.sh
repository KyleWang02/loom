#!/bin/sh
# Loom installer for Linux and macOS
# Usage: curl -sSL https://raw.githubusercontent.com/KyleWang02/loom/main/install.sh | sh

set -e

REPO="KyleWang02/loom"
INSTALL_DIR="/usr/local/bin"

main() {
    # Detect OS
    os="$(uname -s)"
    case "$os" in
        Linux)  os_name="linux" ;;
        Darwin) os_name="macos" ;;
        *)
            echo "Error: unsupported OS: $os" >&2
            exit 1
            ;;
    esac

    # Detect architecture
    arch="$(uname -m)"
    case "$arch" in
        x86_64|amd64)  arch_name="x86_64" ;;
        arm64|aarch64) arch_name="arm64" ;;
        *)
            echo "Error: unsupported architecture: $arch" >&2
            exit 1
            ;;
    esac

    # macOS x86_64 might be Rosetta — warn but proceed
    if [ "$os_name" = "macos" ] && [ "$arch_name" = "x86_64" ]; then
        if sysctl -n sysctl.proc_translated 2>/dev/null | grep -q 1; then
            echo "Note: running under Rosetta 2. Using x86_64 binary."
        fi
    fi

    # Determine version
    if [ -n "$LOOM_VERSION" ]; then
        version="$LOOM_VERSION"
    else
        echo "Fetching latest release..."
        version="$(curl -sSL "https://api.github.com/repos/${REPO}/releases/latest" \
            | grep '"tag_name"' | head -1 | sed 's/.*"tag_name": *"\([^"]*\)".*/\1/')"
        if [ -z "$version" ]; then
            echo "Error: could not determine latest version" >&2
            exit 1
        fi
    fi

    echo "Installing loom ${version} (${os_name}-${arch_name})..."

    # Build download URL
    archive="loom-${version}-${os_name}-${arch_name}.tar.gz"
    url="https://github.com/${REPO}/releases/download/${version}/${archive}"

    # Create temp directory with cleanup trap
    tmpdir="$(mktemp -d)"
    trap 'rm -rf "$tmpdir"' EXIT

    # Download
    echo "Downloading ${url}..."
    if ! curl -sSL --fail -o "${tmpdir}/${archive}" "$url"; then
        echo "Error: download failed. Check that release ${version} exists for ${os_name}-${arch_name}." >&2
        exit 1
    fi

    # Extract
    tar -xzf "${tmpdir}/${archive}" -C "$tmpdir"

    # Install
    binary="${tmpdir}/bin/loom"
    if [ ! -f "$binary" ]; then
        echo "Error: binary not found in archive" >&2
        exit 1
    fi
    chmod +x "$binary"

    # Try /usr/local/bin first, then sudo, then fallback to ~/.local/bin
    if [ -w "$INSTALL_DIR" ]; then
        cp "$binary" "$INSTALL_DIR/loom"
        echo "Installed loom to ${INSTALL_DIR}/loom"
    elif command -v sudo >/dev/null 2>&1; then
        echo "Installing to ${INSTALL_DIR} (requires sudo)..."
        sudo cp "$binary" "$INSTALL_DIR/loom"
        echo "Installed loom to ${INSTALL_DIR}/loom"
    else
        INSTALL_DIR="$HOME/.local/bin"
        mkdir -p "$INSTALL_DIR"
        cp "$binary" "$INSTALL_DIR/loom"
        echo "Installed loom to ${INSTALL_DIR}/loom"
        case ":$PATH:" in
            *":${INSTALL_DIR}:"*) ;;
            *)
                echo ""
                echo "WARNING: ${INSTALL_DIR} is not in your PATH."
                echo "Add it with:  export PATH=\"${INSTALL_DIR}:\$PATH\""
                ;;
        esac
    fi

    echo ""
    echo "Run 'loom --version' to verify the installation."
}

main
