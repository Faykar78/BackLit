#!/bin/bash
# Install keyboard backlight hotkey scripts and configuration

echo "Installing keyboard backlight hotkey scripts..."

# Scripts are already installed in /usr/local/bin by the deb package,
# just need to check if they are executable
sudo chmod +x /usr/local/bin/kb_toggle /usr/local/bin/kb_bright_up /usr/local/bin/kb_bright_down /usr/local/bin/kb_color_cycle 2>/dev/null || true

echo "✓ Scripts are available in /usr/local/bin"

# Install xbindkeys config
if [ -f "/usr/share/backlit/xbindkeysrc" ]; then
    cp "/usr/share/backlit/xbindkeysrc" ~/.xbindkeysrc
    echo "✓ xbindkeys config installed to ~/.xbindkeysrc"
else
    echo "⚠ Could not find /usr/share/backlit/xbindkeysrc"
fi

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
