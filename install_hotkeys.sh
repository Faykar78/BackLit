#!/bin/bash
# Install keyboard backlight hotkey scripts and configuration

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

echo "Installing keyboard backlight hotkey scripts..."

# Install scripts to /usr/local/bin
sudo cp "$SCRIPT_DIR/kb_toggle" /usr/local/bin/
sudo cp "$SCRIPT_DIR/kb_bright_up" /usr/local/bin/
sudo cp "$SCRIPT_DIR/kb_bright_down" /usr/local/bin/
sudo chmod +x /usr/local/bin/kb_toggle /usr/local/bin/kb_bright_up /usr/local/bin/kb_bright_down

echo "✓ Scripts installed to /usr/local/bin"

# Install xbindkeys config
cp "$SCRIPT_DIR/xbindkeysrc" ~/.xbindkeysrc
echo "✓ xbindkeys config installed to ~/.xbindkeysrc"

# Kill existing xbindkeys and restart
pkill xbindkeys 2>/dev/null
xbindkeys
echo "✓ xbindkeys started"

echo ""
echo "Hotkey configuration complete!"
echo "  Fn+* (KP_Multiply) - Toggle backlight"
echo "  Fn+- (KP_Subtract) - Brightness down"
echo "  Fn++ (KP_Add)      - Brightness up"
echo ""
echo "To make xbindkeys start on login, add 'xbindkeys' to your startup applications."
