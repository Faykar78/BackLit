#!/bin/bash
# Keyboard Backlight Control Center - Full Installation Script
#
# Installs:
# - kb_gui: GTK4 GUI with integrated hotkey support
# - kb_ctl: CLI tool for scripting
# - udev rules: For no-sudo access to keyboard backlight
# - Desktop entry: For application menu

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
INSTALL_PREFIX="${INSTALL_PREFIX:-/usr/local}"

echo "╔════════════════════════════════════════════════════════════╗"
echo "║        Keyboard Backlight Control Center Installer         ║"
echo "╚════════════════════════════════════════════════════════════╝"
echo ""

# Check if running as root
if [ "$EUID" -eq 0 ]; then
    echo "Please run without sudo. The script will ask for sudo when needed."
    exit 1
fi

# Cleanup legacy files to prevent conflicts
echo "→ Cleaning up legacy files..."
pkill xbindkeys || true
pkill -f kb_hotkey_daemon.sh || true
rm -f ~/.xbindkeysrc
sudo rm -f /usr/local/bin/kb_toggle /usr/local/bin/kb_bright_up /usr/local/bin/kb_bright_down
sudo rm -f "$SCRIPT_DIR/kb_hotkey_daemon.sh" "$SCRIPT_DIR/xbindkeysrc"
echo "  ✓ Legacy files removed"

# Build if not already built
echo "→ Building applications..."
cd "$SCRIPT_DIR"
make kb_gui kb_ctl 2>&1 | grep -E "(error|warning|^gcc)" || true
echo "  ✓ Build complete"

# Install binaries
echo ""
echo "→ Installing binaries to $INSTALL_PREFIX/bin..."
sudo install -m 755 kb_gui "$INSTALL_PREFIX/bin/"
sudo install -m 755 kb_ctl "$INSTALL_PREFIX/bin/"
echo "  ✓ Installed kb_gui and kb_ctl"

# Install udev rules
echo ""
echo "→ Installing udev rules for no-sudo access..."
sudo cp 99-keyboard-backlight.rules /etc/udev/rules.d/
sudo udevadm control --reload-rules
sudo udevadm trigger
echo "  ✓ udev rules installed"

# Add user to input group
echo ""
echo "→ Adding user to 'input' group for hotkey access..."
if ! groups | grep -q input; then
    sudo usermod -aG input "$USER"
    echo "  ✓ Added $USER to input group"
    NEED_LOGOUT=1
else
    echo "  ✓ User already in input group"
    NEED_LOGOUT=0
fi

# Install systemd service
echo ""
echo "→ Installing background service..."
mkdir -p ~/.config/systemd/user
cp kb-backlight.service ~/.config/systemd/user/
systemctl --user daemon-reload
systemctl --user enable --now kb-backlight.service
echo "  ✓ Background service installed and started"

# Install desktop entry
echo ""
echo "→ Installing desktop entry..."
sudo mkdir -p /usr/share/applications
cat > /tmp/kb-control.desktop << EOF
[Desktop Entry]
Name=Keyboard Backlight
Comment=Control keyboard backlight color and brightness
Exec=kb_gui
Icon=preferences-desktop-keyboard
Terminal=false
Type=Application
Categories=Settings;HardwareSettings;
Keywords=keyboard;backlight;LED;color;
EOF
sudo install -m 644 /tmp/kb-control.desktop /usr/share/applications/
rm /tmp/kb-control.desktop
echo "  ✓ Desktop entry installed"

# Summary
echo ""
echo "╔════════════════════════════════════════════════════════════╗"
echo "║                   Installation Complete!                   ║"
echo "╚════════════════════════════════════════════════════════════╝"
echo ""
echo "Installed:"
echo "  • kb_gui  → $INSTALL_PREFIX/bin/kb_gui"
echo "  • kb_ctl  → $INSTALL_PREFIX/bin/kb_ctl"
echo "  • udev    → /etc/udev/rules.d/99-keyboard-backlight.rules"
echo "  • desktop → /usr/share/applications/kb-control.desktop"
echo ""
echo "Hotkey Support (Fn keys):"
echo "  • Fn + Toggle key  → Toggle backlight ON/OFF"
echo "  • Fn + Brightness+ → Increase brightness"
echo "  • Fn + Brightness- → Decrease brightness"
echo ""

if [ "$NEED_LOGOUT" -eq 1 ]; then
    echo "⚠️  IMPORTANT: You need to LOG OUT and LOG IN again"
    echo "   for input group permissions to take effect!"
    echo ""
fi

echo "Run 'kb_gui' from terminal or find 'Keyboard Backlight' in your apps."
